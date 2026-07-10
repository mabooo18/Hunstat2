/*
    AUTHOR: Richard Morrison
    EMAIL: instruments4chem@gmail.com

    DISCLAIMER:
    Instruments4Chem code, firmware, and software is released under the MIT License
    (http://opensource.org/licenses/MIT).

    The MIT License (MIT)

    Copyright (c) 2024 Instruments4Chem, Melbourne, AUSTRALIA

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights to
    use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is furnished to do
    so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/
// D:\Work\VamosPista\Richard\AD5940_RM_EIS161224\AD5940_RM_EIS161224.ino

#define USE_250202_Version  1
#define SET_VOLTAGE_ON_RP2040_D6  0
// Code to run EIS experiments using an Analog Devices AD5941 chip
// Richard J. S. Morrison, December 10th, 2024

#include "ad5940.h"
#include "AD5940.h"
#include <stdio.h>
#include "string.h"
#include <Adafruit_NeoPixel.h>
#include "utilities.h"
#include "hunstat_status_utils.h"
#include "src/ad5940/debug.h"
#include "hunstat_data_storage.h"
#include "src/data_storage/measurement_buffer.h"
#include "src/electrochemical_methods/electrochemical_methods.h"
#include "src/command_processing/command_processing.h"
#include "src/setup/ad5941_setup.h"
#include "src/communication/communication.h"
#include "src/hardware/adc_control.h"
#include "src/hardware/wave_gen.h"
#include "src/hardware/gain_control.h"
#include "src/interface/led_interface.h"

extern uint32_t AD5940_TakeMeasurement(uint32_t afectrl_bits, uint32_t afectrl_readybit, uint32_t afectrl_resultType, int32_t *time_out);

// V start/stop for ramptest (cv.cpp)
extern float V_start;
extern float V_stop;
extern float Estep;             /**< The potential difference between each step in mV --> determines StepNumber. Ideally a multiple of DAC12BITVOLT_1LSB*/
extern float ScanRate;          /**< Slope of the ramp in mV/sec --> determines RampDuration*/
extern uint16_t CycleNumber;    /**< The number of cycles to repeat the ramp test */
extern void  cvSetup (float start, float stop);

int Power = 11;
int PIN  = 12;
#define NUMPIXELS 1
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);
C_DataStorage g_Data;
C_Communication g_Comm;
C_AD5941_Setup g_Setup;

// Define the ADC input pins for WE and RE
#define WEpin A0            // ADC input pin for WE
#define REpin A3            // ADC input pin for RE

const float ADC_AVDD = 3300.0;              // Power supply for analogue-to-digital converter, nominal voltage 3.3V
const int ADCresolution = 12;               // RP2040 ADC has 12 bit resolution
const int ADCFullValue = (1 << ADCresolution) - 1;
float WEmV = 0.0;                           // expected voltage on WE, set by the host in OCPCalibration mode
float WEfrom = 0.0;                         // OCP calibration start voltage on WE, set by the host
float WEto = 0.0;                           // OCP calibration stop voltage on WE, set by the host
float WEstep = 0.0;                         // OCP calibration voltage step on WE, set by the host
uint32_t HSDACDAT;                          // used in OCP calculation
BoolFlag ocpCalibration = bFALSE;           // calibration of OCP calculation is in progress
BoolFlag ocpCalibrationCycling = bFALSE;    // a series calibration of OCP calculations are to be performed from WEfrom to WEto with step of WEstep
float constA = 32772.0;                     // used in OCP calculation
float constB = -26.719;                     // used in OCP calculation
bool useConstAB = false;                    // tells if constA & B is to be used when calculating OCP

#define ADCPGA_GAIN_SEL ADCPGA_1P5

// See the E command for a description of the following two parameters
// They provide settling delays and were added during testing
// Current setup is two fixed delays of 2 ms and 1 ms - but changing them
// didn't seem to make any difference however they have been retained for now

uint32_t settling_parameter = 2;
uint16_t adc_delay_ms = 1;
uint32_t SYS_CLOCK_HZ = 16000000UL;

extern int csPin;
extern int resetPin;
extern int intPin;

int receivedNumber = 0;
char commandType = 0;
bool hex = false;

FreqParams_Type FP;

uint8_t pga_gain = 1, tia_rf = 3, EIS_mode, vzero = 26, use_variable_gain = 0;   // EIS_mode: 0 - Rz, 1 - Rcal
uint16_t nfreqs = 50, vbias = 1664, amplitude = 126, offset = 4040, OCP_npts ;
uint16_t    settling_delay_ms ;                                                  //  MS: i is deleted (it is very dangerous to declare cycle variable in global scope!)
uint32_t freqlo, freqhi, _CGmax = 30000, _CGmin = 7500, OCP_sum, ADCCON;

uint32_t ADCFILTERCON, DFTCON, AFECON;

AFERefCfg_Type aferef_cfg;

// value of RCAL on the board in Ohm
float fRcal = 10000.0;
float fAmplitude = 50.0,
    fBias = 0.0,
    fOffset = 0.0;

HSLoopCfg_Type HpLoopCfg;
ADCFilterCfg_Type adc_filter;
ADCBaseCfg_Type adc_base;
DFTCfg_Type DftCfg;
FIFOCfg_Type fifo_cfg;
CLKCfg_Type clk_cfg;
ClksCalInfo_Type clks_cal;

// Set proper bit in verbose to show and log changes in internal states of variables
// 1 - activates Info to Serial
// 2.. - activates info and log channels
extern uint32_t verbose;

// bool SeeedStatMode is status flag. By default it is false to work with LabVIEW program (EIS_161224.exe). When using with SeeedStat, 'D' command (setting frequency range)
// sent by SeeedStat sets bool SeeedStatMode to true, and in this case real and imaginary values are sent in ASCII when executing measurement (P command).
// After measurement, bool SeeedStatMode is set back to false.
bool SeeedStatMode = false;

extern char history[];
extern char* pHistory;

// measurement buffer is managed through the modular data storage layer

#if SET_VOLTAGE_ON_RP2040_D6
void RP2040_SetVoltageOnPin(int pin, float mV, bool withDelay)
{
    if (mV <= 1.0)
    {
        pinMode(pin, INPUT);
    }
    else
    {
        // Set the PWM pin as outputs
        pinMode(pin, OUTPUT);
        
        // input voltage on AD5940 shall be between 0.2 and 2.1 V
//*        mV = constrain(mV, 200.0, 2100.0);
        
        const int resolution = 16;                          // resolution of PWM DAC in RP2040
        analogWriteResolution(resolution);
        int value = ((1 << resolution) - 1) * mV / 3300.0;  // supply voltage is 3.3V
        analogWrite(pin, value);                            // Set duty cycle
    }
    
    if (withDelay)
    {
        delay(3000);
    }
}
#endif

uint32_t SetDACLevel(float mV)
{
    uint32_t HSDACCON = AD5940_ReadReg(REG_AFE_HSDACCON);
    float INAMPGNMDE = (HSDACCON & BITM_AFE_HSDACCON_INAMPGNMDE) == BITM_AFE_HSDACCON_INAMPGNMDE ? 0.25 : 2.0;
    float ATTENEN = (HSDACCON & BITM_AFE_HSDACCON_ATTENEN) == BITM_AFE_HSDACCON_ATTENEN ? 0.2 : 1.0;
    // HSDACDAT = mV * 2048.0 / (404.4 * INAMPGNMDE * ATTENEN) + 2048.0;  // formula containing 404.4 mV for direct write to HSDACDAT, see page 43 of AD5940/AD5941.pdf
    HSDACDAT = mV * 2047.0 / (808.0 * INAMPGNMDE * ATTENEN);     // formula containing 808.8 mV for use with WG, see page 43 of AD5940/AD5941.pdf
    //HSDACDAT = constrain(HSDACDAT, 0x200, 0xe00);

    WGCfg_Type wgInit;
    wgInit.WgType = WGTYPE_MMR;         // Direct write to DAC using register
    wgInit.GainCalEn = bTRUE;           // Enable Gain calibration
    wgInit.OffsetCalEn = bFALSE;        // Disable offset calibration
    wgInit.WgCode = HSDACDAT;
    AD5940_WGCfgS(&wgInit);

    Log(0x80, __LINE__, "mV=%.2f HSDACCON=0x%lX INAMPGNMDE=%.2f ATTENEN=%.1f HSDACDAT=%ld(0x%lX) ",
                         mV,     HSDACCON,      INAMPGNMDE,     ATTENEN,     HSDACDAT, constrain(HSDACDAT, 0x200, 0xe00));
                         
#if SET_VOLTAGE_ON_RP2040_D6
    RP2040_SetVoltageOnPin(D6, mV, false);
#endif

    return HSDACDAT;
}

// This routine finds optimum values of TIA_Rf and PGA_gain given an input frequency
// The algorithm makes use of combined gain(CG) values determined by multiplying the gain resistor
// value in ohms by the PGA gain factor.  The user specifies maximum and minimum values for CG -
// the former assumed to apply at 0.1 Hz and the latter at 100 kHz.  From these values the Slope(m)
// and intercept(c) based on the relation log(CG) = m * log(frequency) + c are then used to calculate
// a log(CG) value and then an optimum CG for the given input frequency.
//
// The code then scans through all possible TIA_Rf and PGA gain settings to find the one that is closest to
// this optimum CG value.  The two values passed back are the AD5941 codes needed to correctly set
// these two parameters.  The closest combined gain found is also returned.
//
// for example - if we wish to set Rf = 40k and PGA = 9 at 0.1 Hz - CGmax becomes 360000
// and we then require to set Rf = 200 and PGA = 1 at 100 kHz - CGmin becomes 200

void FindOptimum_Rf_PGA(uint32_t CGmax, uint32_t CGmin, float freq, uint8_t* TIA_Rf, uint8_t* PGA_gain, float* closest_CG)
{
    Hardware_FindOptimum_Rf_PGA(CGmax, CGmin, freq, TIA_Rf, PGA_gain, closest_CG);
}

// Important note - January 25th, 2025
// This routine is now used instead of calling the AD5940_GetFreqParameters routine
// which did not behave nicely when bias voltage was applied !!!
//
// Configures ADC filter and DFT parameters
// Values here can be customized according to various frequency intervals
//
// If doing so the key requirements are -:
// ADC Sample rate must satisfy the Nyquist theorem (ideally fs >> 2*fexc)
// DFT engine must sample for at least one full period but preferable many

// For information here are the possible SINC2OSR and SINC3OSR constants -:
// SINC2OSR allowed values are 22,44,89,178,267,533,640,667,800,889,1067 and 1333
// SINC3OSR allowed values are 2, 4 and 5

// Direct DAC output to CE pin
// this functionality is taken from ad5940-examples-master\examples\AD5940_WG\AD5940_WGArbitrary.c
void ConfigureHSLoopCfg(uint32_t hsDacDat)
{
    HSLoopCfg_Type HpLoopCfg;
    AD5940_StructInit(&HpLoopCfg, sizeof(HSLoopCfg_Type));
    
    HpLoopCfg.HsDacCfg.ExcitBufGain = EXCITBUFGAIN_2;
    HpLoopCfg.HsDacCfg.HsDacGain = HSDACGAIN_1;
    HpLoopCfg.HsDacCfg.HsDacUpdateRate = 7;

    HpLoopCfg.HsTiaCfg.DiodeClose = bFALSE;
    HpLoopCfg.HsTiaCfg.HstiaBias = HSTIABIAS_1P1;
    HpLoopCfg.HsTiaCfg.HstiaCtia = 16; /* 16pF */
    HpLoopCfg.HsTiaCfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    HpLoopCfg.HsTiaCfg.HstiaDeRtia = HSTIADERTIA_TODE;    /* Connect HSTIA output to DE0 pin */
    HpLoopCfg.HsTiaCfg.HstiaRtiaSel = HSTIARTIA_200;

    HpLoopCfg.SWMatCfg.Dswitch = SWD_CE0;
    HpLoopCfg.SWMatCfg.Pswitch = SWP_CE0;
    HpLoopCfg.SWMatCfg.Nswitch = SWN_SE0LOAD;
    HpLoopCfg.SWMatCfg.Tswitch = SWT_TRTIA | SWT_SE0LOAD;

    HpLoopCfg.WgCfg.WgType = WGTYPE_MMR;    /* We use sequencer to update DAC data point by point. */
    HpLoopCfg.WgCfg.GainCalEn = bFALSE;
    HpLoopCfg.WgCfg.OffsetCalEn = bFALSE;
    HpLoopCfg.WgCfg.WgCode = hsDacDat;
    AD5940_HSLoopCfgS(&HpLoopCfg);
}

void Config_AD5941_OCP_Measurement(float wemV)
{
    AD5940_PGA_Calibration();
    AD5940_AFEPwrBW(AFEPWR_LP, AFEBW_250KHZ);

    // Initialize ADC filters ADCRawData-->SINC3-->SINC2+NOTCH
    // In this configuration both SINC3 and SINC2 blocks are active
    // Data rate after the SINC2 filter will be 800kSPS/4/1333 = 150.0375Hz

    adc_base.ADCPga = ADCPGA_GAIN_SEL;
    AD5940_ADCBaseCfgS(&adc_base);

    adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
    adc_filter.ADCSinc2Osr = ADCSINC2OSR_1333;
    adc_filter.ADCAvgNum = ADCAVGNUM_2;         // irrelevant as only used by DFT
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = bTRUE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    AD5940_ADCFilterCfgS(&adc_filter);

    AD5940_ADCMuxCfgS(ADCMUXP_VSE0, ADCMUXN_AIN3);          // RE is input on AIN3/BUF_VREF1V8 into ADC MUXSELN
    
    AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    ///AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV | AFECTRL_WG | AFECTRL_DACREFPWR | AFECTRL_EXTBUFPWR | AFECTRL_INAMPPWR | AFECTRL_DCBUFPWR | AFECTRL_HSDACPWR, bFALSE);
    
    OutputPulse(D4, 300);
    if (ocpCalibration == bTRUE)
    {
        digitalWrite(D4, HIGH);
        uint32_t hsDacDat = SetDACLevel(wemV);
        digitalWrite(D4, LOW);

        ConfigureHSLoopCfg(hsDacDat);
    }

    AD5940_AFECtrlS(AFECTRL_DACREFPWR, ocpCalibration);
    AD5940_AFECtrlS(AFECTRL_EXTBUFPWR | AFECTRL_INAMPPWR | AFECTRL_HSTIAPWR | AFECTRL_HSTIAPWR | AFECTRL_HSDACPWR, ocpCalibration);
    AD5940_AFECtrlS(AFECTRL_WG, ocpCalibration);
    AD5940_AFECtrlS(AFECTRL_DCBUFPWR, ocpCalibration);

    // n.b. There are no calls to configure the LP loop here
    // This is important as we don't want potentiostatic control
    // affecting the RE pin during the OCP measurements
}

uint32_t Do1_AD5941_OCP_Measurement()
{
    uint32_t result = 0;
    digitalWrite(D5, HIGH);
#if 0
    while(AD5940_INTCTestFlag(AFEINTC_1, AFEINTSRC_SINC2RDY) == bFALSE);
    AD5940_INTCClrFlag(AFEINTSRC_SINC2RDY);
    result = AD5940_ReadAfeResult(AFERESULT_SINC2);
#else
    int32_t time_out = 1000;
    result = AD5940_TakeMeasurement(AFECTRL_ADCPWR | AFECTRL_ADCCNV, AFEINTSRC_SINC2RDY, AFERESULT_SINC2, &time_out);
#endif    
    digitalWrite(D5, LOW);

    return result;
}

float RP2040_MeasurePin(int pin)
{
    uint16_t value = analogRead(pin);
    float mV = ADC_AVDD * value / ADCFullValue;
    return mV;
}

float RP2040_MeasureOCP()
{
    float wemV = RP2040_MeasurePin(WEpin);
    float remV = RP2040_MeasurePin(REpin);
    return remV - wemV;                     // sending Vre-Vwe according to 'T' command response
}

void PrintOCP(float we, float ocp1, float ocpMeasured)
{
    if (SeeedStatMode)
    {
        char buffer[500];
        sprintf(buffer, "%04.0f,%05.1f,%03.1f", we, ocp1, ocpMeasured);
        Serial.println(buffer);
    }
    else
    {
        Serial.write((uint8_t*)&we, 4);
        Serial.write((uint8_t*)&ocp1, 4);
        Serial.write((uint8_t*)&ocpMeasured, 4);
    }
}

void Do_AD5941_OCP_Measurement()
{
    // Baca register switch matrix langsung
    uint32_t dsw = AD5940_ReadReg(REG_AFE_DSWFULLCON);
    uint32_t psw = AD5940_ReadReg(REG_AFE_PSWFULLCON);
    uint32_t nsw = AD5940_ReadReg(REG_AFE_NSWFULLCON);
    uint32_t tsw = AD5940_ReadReg(REG_AFE_TSWFULLCON);
    
    Log(0x80, __LINE__, "REG_AFE_SWCON=0x%lX Dswitch=0x%lX Nswitch=0x%lX Pswitch=0x%lX Tswitch=0x%lX ",
          AD5940_ReadReg(REG_AFE_SWCON), dsw, nsw, psw, tsw);

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV, bTRUE);  // Power-up ADC and start conversions

    delay(5);

    digitalWrite(D4, HIGH);
    if (ocpCalibrationCycling == bTRUE)
    {
        float step = WEfrom < WEto ? abs(WEstep) : -abs(WEstep);
        for (float we = WEfrom; WEfrom < WEto ? we <= WEto : we >= WEto; we += step)
        {
            delayMicroseconds(500);
            digitalWrite(D4, HIGH);

            SetDACLevel(we);
            
            uint32_t ocp_sum = 0;
            float ocpMeasured = 0.0;
            for(uint16_t i = 0; i < OCP_npts; i++)
            {
                ocp_sum += Do1_AD5941_OCP_Measurement();
                ocpMeasured += RP2040_MeasureOCP();
            }

            // sending Vre-Vwe instead of Vwe-Vre
            PrintOCP(we, -(static_cast<float>(ocp_sum) / OCP_npts), ocpMeasured / OCP_npts);

            digitalWrite(D4, LOW);
        }
    }
    else
    {
        OCP_sum = 0;
        for(uint16_t i = 0; i < OCP_npts; i++)
        {
            delayMicroseconds(500);
            OCP_sum += Do1_AD5941_OCP_Measurement();
        }
    }
        
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV, bFALSE);   // Power-down ADC and stop conversions

    digitalWrite(D4, LOW);

    ADCCON = AD5940_ReadReg(REG_AFE_ADCCON);
    
    if (ocpCalibrationCycling != bTRUE)
    {
        Serial.print('Z');
    }
    ocpCalibrationCycling = bFALSE;
    SeeedStatMode = false;
}

// Calculate Open Circuit Potential based on AD5940/AD5941.pdf (Rev. D) page 56
float CalculateOCP()
{
    float ocp_mV;
    float ocp_1 = static_cast<float>(OCP_sum) / OCP_npts;
    if (SeeedStatMode)
    {
        ocp_1 *= -1.0;    // sending Vre-Vwe instead of Vwe-Vre
    }
    
    if (useConstAB)
    {
        float ocp_npts = static_cast<float>(OCP_npts);
        
        ocp_mV = (ocp_1 - constA) / constB;

        Log(0x80, __LINE__, "ocp_1=%.2f constA=%.3f constB=%.3f ocp_mV=%.6f ",
                             ocp_1,     constA,     constB,     ocp_mV);
    }
    else
    {
        uint8_t GNPGA = (ADCCON & BITM_AFE_ADCCON_GNPGA) >> BITP_AFE_ADCCON_GNPGA;
        float Vref_mV = (GNPGA == 1) ? 1835.0 : 1820.0;       // Vref = 1.835V or 1.82V depending on GNPGA
        float PGA_G_values[] = { 1.0, 1.5, 2.0, 4.0, 9.0, 9.0 };
        float PGA_G = PGA_G_values[GNPGA];                    // PGA gain, depends on GNPGA bits of ADCCON register

        uint8_t MUXSELP = (ADCCON & BITM_AFE_ADCCON_MUXSELP) >> BITP_AFE_ADCCON_MUXSELP;    // 0xE: Voltage at SE0 - measured at pin
        uint8_t MUXSELN = (ADCCON & BITM_AFE_ADCCON_MUXSELN) >> BITP_AFE_ADCCON_MUXSELN;    // 0x7: AIN3/BUF_VREF1V8

        float ocp_npts = static_cast<float>(OCP_npts);
        const uint8_t MUXSELN_VBIAS_CAP = 8;                                                // VBIAS_CAP value in MUXSELN
        float VBIAS_CAP_mV = (MUXSELN == MUXSELN_VBIAS_CAP) ? 1110.0 : 0.0;                 // VBIAS_CAP (1.11V) is to be added only if MUXSELN equals to 8
        const float ADC_MIDDLE_AND_ACTIVE_RANGE = 1 << 15;
        
        // based on formulae (12) and (13) on page 56 in AD5940/AD5941.pdf:
        ocp_mV = (ocp_1 - ADC_MIDDLE_AND_ACTIVE_RANGE) * (Vref_mV / PGA_G) / ADC_MIDDLE_AND_ACTIVE_RANGE + VBIAS_CAP_mV;

        Log(0x80, __LINE__, "ADCCON=0x%X GNPGA=%i MUXSELP=0x%X MUXSELN=0x%X VBIAS_CAP=%.2f Vref_mV=%.0f PGA_G=%.1f ocp_1=%.2f ocp_mV=%.6f ",
                             ADCCON,     GNPGA,   MUXSELP,     MUXSELN,   VBIAS_CAP_mV/1000, Vref_mV,   PGA_G,     ocp_1,     ocp_mV);
    }
    
    Log(0x100, __LINE__, "WEmV=%.0f HSDACDAT=%d ocp_1=%.2f ocp_mV=%.3f ",
                          WEmV,     HSDACDAT,   ocp_1,     ocp_mV);
    return ocp_mV;
}

void Config_LPLOOP()
{
    LPDACCfg_Type lpdac_cfg;
    lpdac_cfg.LpdacSel = LPDAC0;
    lpdac_cfg.LpDacVbiasMux = LPDACVBIAS_12BIT; /* Use Vbias to tuning BiasVolt. */
    lpdac_cfg.LpDacVzeroMux = LPDACVZERO_6BIT;  /* Vbias-Vzero = BiasVolt */
    lpdac_cfg.DacData6Bit = vzero;              /* Set Vzero to middle scale. */
    lpdac_cfg.DacData12Bit = vbias;
    lpdac_cfg.DataRst = bFALSE;                 /* Do not reset data register */
    lpdac_cfg.LpDacSW = LPDACSW_VBIAS2LPPA | LPDACSW_VBIAS2PIN | LPDACSW_VZERO2LPTIA | LPDACSW_VZERO2PIN | LPDACSW_VZERO2HSTIA;
    lpdac_cfg.LpDacRef = LPDACREF_2P5;
    lpdac_cfg.LpDacSrc = LPDACSRC_MMR;      /* Use MMR data, we use LPDAC to generate bias voltage for LPTIA - the Vzero */
    lpdac_cfg.PowerEn = bTRUE;              /* Power up LPDAC */
    AD5940_LPDACCfgS(&lpdac_cfg);
}

// The HSDAC has a number of different gain settings as shown below.
// The HSDAC needs to be calibrated seperately for each gain setting. HSDAC has two power
// modes. There are seperate offset registers for both, DACOFFSET and DACOFFSETHP. The
// HSDAC needs to be calibrated for each mode.

// HSDACCON[12] |   HSDACCON[0] |   Output Range    |
// 0            |   0           |   +-607mV         |
// 1            |   0           |   +- 75mV         |
// 1            |   1           |   +- 15.14mV      |
// 0            |   1           |   +-121.2mV       |


// power_mode = 0 for AFEPWR_LP, mode = 1 for AFEPWR_HP
// pgag = 0 (1), 1 (1.5), 2 (2), 3 (4) and 4 (9)

void Calibrate_HSDAC(uint8_t power_mode, uint8_t pga_g)
{
    HSDACCal_Type hsdac_cal;
    ADCPGACal_Type adcpga_cal;
    CLKCfg_Type clk_cfg;

    AD5940_AFEPwrBW(power_mode, AFEBW_250KHZ);    // Changed this line, RJSM 111224

    clk_cfg.ADCClkDiv = ADCCLKDIV_1;
    clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
    clk_cfg.SysClkDiv = SYSCLKDIV_1;
    clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
    clk_cfg.HfOSC32MHzMode = bTRUE;
    clk_cfg.HFOSCEn = bTRUE;
    clk_cfg.HFXTALEn = bFALSE;
    clk_cfg.LFOSCEn = bTRUE;
    AD5940_CLKCfg(&clk_cfg);

    // ADC Offset Cal
    adcpga_cal.AdcClkFreq = 16000000;
    adcpga_cal.ADCPga = pga_g;      // Changed this line, RJSM 111224
    adcpga_cal.ADCSinc2Osr = ADCSINC2OSR_1333;
    adcpga_cal.ADCSinc3Osr = ADCSINC3OSR_4;
    adcpga_cal.PGACalType = PGACALTYPE_OFFSET;
    adcpga_cal.TimeOut10us = 1000;
    adcpga_cal.VRef1p11 = 1.11;
    adcpga_cal.VRef1p82 = 1.82;
    AD5940_ADCPGACal(&adcpga_cal);

    // 607mV Range Cal
    hsdac_cal.ExcitBufGain = EXCITBUFGAIN_2;
    hsdac_cal.HsDacGain = HSDACGAIN_1;
    hsdac_cal.AfePwrMode = power_mode;    // Changed this line, RJSM 111224
    hsdac_cal.ADCSinc2Osr = ADCSINC2OSR_1333;
    hsdac_cal.ADCSinc3Osr = ADCSINC3OSR_4;
    AD5940_HSDACCal(&hsdac_cal);

    // ADC Offset Cal for the PGA gain setting passed in
    adcpga_cal.ADCPga = pga_g;
    AD5940_ADCPGACal(&adcpga_cal);

    // 125mV Range Cal
    hsdac_cal.ExcitBufGain = EXCITBUFGAIN_2;
    hsdac_cal.HsDacGain = HSDACGAIN_0P2;
    AD5940_HSDACCal(&hsdac_cal);

    // 75mV Range Cal
    hsdac_cal.ExcitBufGain = EXCITBUFGAIN_0P25;
    hsdac_cal.HsDacGain = HSDACGAIN_1;
    AD5940_HSDACCal(&hsdac_cal);

    // 15mV Range Cal
    hsdac_cal.ExcitBufGain = EXCITBUFGAIN_0P25;
    hsdac_cal.HsDacGain = HSDACGAIN_0P2;
    AD5940_HSDACCal(&hsdac_cal);
}

bool IsAD5940Calculating()
{
    return !(AD5940_INTCTestFlag(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH));
}

void eisScan(uint8_t eisMode)
{
    Utils_SetStatusLed(WHITE);
    Calibrate_HSDAC(0, pga_gain);

#if USE_250202_Version
    AD5940_WriteReg(REG_AFE_ADCMAX, 61440);  // important - we need to set up a condition for ADCMAXERR detection !!!
    AD5940_WriteReg(REG_AFE_ADCMAXSMEN, 61184);
#endif

    if(eisMode == 0) Utils_SetStatusLed(BLUE);    // measuring Rz
    else Utils_SetStatusLed(RED);                 // measuring Rcal

    float flo = (float) freqlo / 1000.0;
    float fhi = (float) freqhi / 1000.0;

    float log_start = log10(flo);
    float log_end = log10(fhi);
    float log_step = (log_end - log_start) / (nfreqs - 1);

    for(uint16_t i = 0; i < nfreqs; i++)
    {
        // Very important to initialize the AD5941 before any ADC parameter changes
        AD5941_InitAll();

        Config_LPLOOP(); // sets vbias and vzero

        float frequency = pow(10, log_start + i * log_step);
        Do_WaveGen(eisMode, frequency, amplitude, tia_rf);  // see AD5940_Wavegen131124 for test code

        // Set power mode
        if(frequency > 80000)
        {
#if USE_250202_Version
            HpLoopCfg.HsDacCfg.ExcitBufGain = EXCITBUFGAIN_2;  // or could be EXCITBUFGAIN_0P25
            HpLoopCfg.HsDacCfg.HsDacGain = HSDACGAIN_1;  // or could be HSDACGAIN_0P2
#else
            HpLoopCfg.HsDacCfg.ExcitBufGain = excitbuf_gain;
            HpLoopCfg.HsDacCfg.HsDacGain = hsdac_gain;
#endif
            HpLoopCfg.HsDacCfg.HsDacUpdateRate = 0x07;
            AD5940_HSLoopCfgS(&HpLoopCfg);

            clk_cfg.HfOSC32MHzMode = bTRUE;
            AD5940_CLKCfg(&clk_cfg);

            AD5940_HPModeEn(bTRUE); // 32MHz oscillator
        }
        else
        {
            HpLoopCfg.HsDacCfg.ExcitBufGain = EXCITBUFGAIN_2;  // or could be EXCITBUFGAIN_0P25
            HpLoopCfg.HsDacCfg.HsDacGain = HSDACGAIN_1;  // or could be HSDACGAIN_0P2
            HpLoopCfg.HsDacCfg.HsDacUpdateRate = 0x1B;
            AD5940_HSLoopCfgS(&HpLoopCfg);

            clk_cfg.HfOSC32MHzMode = bFALSE;
            AD5940_CLKCfg(&clk_cfg);

            AD5940_HPModeEn(bFALSE); // 16 Mhz oscillator
        }

        init_AD5940_ADC(frequency); // Configure ADC filter and DFT parameters --- GetFreqParameters is not reliable !!!

        if (SeeedStatMode)
        {
            digitalWrite(D4, HIGH); // show start of delay set with settling_parameter
        }

        // Provide a delay to ensure the waveform generator and ADCfilters have settled
        // 3 options are supported -:
        // 0 = no delay, 1 = frequency-based delay (1 period), >1 = fixed delay in msec

        if(settling_parameter > 1)
        {
            Delay(settling_parameter, 16, 0, 16);
        }
        else if(settling_parameter == 1)
        {
            if(frequency < 100.0)settling_delay_ms = (uint16_t)(1000/frequency) ;
            else settling_delay_ms = 10; // fixed settling above 100 Hz

            Delay(settling_delay_ms, 16, 0, 16);
        }

        if (SeeedStatMode)
        {
            digitalWrite(D4, LOW);  // show end of delay set with settling_parameter
        }

        AD5940_AFECtrlS(AFECTRL_ADCCNV | AFECTRL_DFT, bFALSE);
        AD5940_AFECtrlS(AFECTRL_ADCPWR, bTRUE); // ADC power

        if (adc_delay_ms)
        {
            Delay(adc_delay_ms, -16, 0, -16); // ADC power-up time
        }

        AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_ALLINT, bTRUE); // Enable all interrupts
        AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

        digitalWrite(D4, HIGH);
        AD5940_AFECtrlS(AFECTRL_ADCCNV | AFECTRL_DFT, bTRUE);  // Start ADC and DFT

        // while(!(AD5940_INTCTestFlag(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH)))
        // {}
        Delay(&IsAD5940Calculating, 16, 0, 16);

        // Disable ADCCNV and DFT
        AD5940_AFECtrlS(AFECTRL_ADCCNV | AFECTRL_DFT, bFALSE);

        uint32_t DFTREAL, DFTIMAG;
        //DFTREAL = AD5940_ReadReg(REG_AFE_DFTREAL);
        //DFTIMAG = AD5940_ReadReg(REG_AFE_DFTIMAG);
        DFTREAL = AD5940_ReadReg(REG_AFE_DATAFIFORD);
        DFTIMAG = AD5940_ReadReg(REG_AFE_DATAFIFORD);
        digitalWrite(D4, LOW);

        AddMeasurementToHistory(DFTREAL, DFTIMAG);

        // Pulse D5 if ADC has gone out of range
        if(AD5940_INTCTestFlag(AFEINTC_0, AFEINTSRC_ADCMINERR | AFEINTSRC_ADCMAXERR))
        {
            OutputPulse(D5, 10);
        }

        AD5940_INTCClrFlag(AFEINTSRC_ALLINT);  // Clear all interrupts

        if (SeeedStatMode)
        {
            float real = ToFloat(DFTREAL);
            float imag = ToFloat(DFTIMAG);
            Log(0x20, __LINE__, "eisMode=%i frequency=%.2f DFTREAL=0x%X DFTIMAG=0x%X real=%.2f imag=%.2f ",
                                 eisMode,   frequency, DFTREAL & 0x3ffff, DFTIMAG & 0x3ffff, real, imag);

            if (verbose & 1)
            {
                Serial.print(i + 1);
                Serial.print("=");

                Serial.print(real);
                Serial.print(",");
                Serial.print(imag);
                Serial.println(",");
            }

            AppendMeasurement(real, imag);
        }
        else
        {
            Serial.write((uint8_t*)&DFTREAL, 4);
            Serial.write((uint8_t*)&DFTIMAG, 4);
        }
    }

    Utils_SetStatusLed(GREEN);     // done
    Utils_SetStatusPixels(0, 255, 0);
}

void SeeedStatScan()
{
    AD5940_PGA_Calibration();

    ResetMeasurementBuffer();
    eisScan(EIS_mode = 0);

    ResetMeasurementBuffer();
    eisScan(EIS_mode = 1);

    CalculateNyquistCurve();
}

void CalculateMagAndPhase(float* pRealAndImag, float& mag, float& phase)
{
    float real = *pRealAndImag++;
    float imag = *pRealAndImag;

    mag = sqrt(real * real + imag * imag);
    phase = atan2(-imag, real);
}

void CalculateNyquistPoint(float* pRZ, float* pRCAL)
{
    float RZmag, RZphase;
    CalculateMagAndPhase(pRZ, RZmag, RZphase);

    float RCALmag, RCALphase;
    CalculateMagAndPhase(pRCAL, RCALmag, RCALphase);

    float ZUnknownMag = abs(RCALmag / RZmag) * fRcal;
    float ZUnknownPhase = RZphase - RCALphase;

    float nyquistPointX = ZUnknownMag * cos(ZUnknownPhase);
    float nyquistPointY = ZUnknownMag * sin(ZUnknownPhase);

    Log(0x20, __LINE__, "RZmag=%.2f RZphase=%.2f", RZmag, RZphase);
    Log(0x20, __LINE__, "RCALmag=%.2f RCALphase=%.2f", RCALmag, RCALphase);
    Log(0x20, __LINE__, "ZUnknownMag=%.2f ZUnknownPhase=%.2f", ZUnknownMag, ZUnknownPhase);
    Log(0x20, __LINE__, "nyquistPointX=%.2f nyquistPointY=%.2f", nyquistPointX, nyquistPointY);

    char buf[100];
    sprintf(buf, "%.3f,%.3f,", nyquistPointX, nyquistPointY);
    Serial.println(buf);
    Serial.flush();
    delay(1);
}

void CalculateNyquistCurve()
{
    float* pRZ = Measurements;                              // points to real and imag pairs of Rz measurements
    float* pRCAL = &Measurements[2 * numberOfMeasurements]; // points to real and imag pairs of Rcal measurements
    for (int i = 0; i < numberOfMeasurements; ++i)
    {
        CalculateNyquistPoint(pRZ, pRCAL);
        pRZ += 2;                                           // points to next pair of real and imag values of Rz measurements
        pRCAL += 2;                                         // points to next pair of real and imag values of Rcal measurements
    }
}

// CA/SWV/DPV implementation now lives in src/electrochemical_methods.

void setup(void)
{
    AD5940_StructInit(&HpLoopCfg, sizeof(HSLoopCfg_Type));
    AD5940_StructInit(&adc_filter, sizeof(ADCFilterCfg_Type));
    AD5940_StructInit(&adc_base, sizeof(ADCBaseCfg_Type));
    AD5940_StructInit(&DftCfg, sizeof(DFTCfg_Type));
    AD5940_StructInit(&fifo_cfg, sizeof(FIFOCfg_Type));
    AD5940_StructInit(&clk_cfg, sizeof(CLKCfg_Type));
    AD5940_StructInit(&clks_cal, sizeof(ClksCalInfo_Type));

    g_Data.Begin();
    g_Setup.Begin();
    g_Comm.Begin(1000000, &g_Data);

    amplitude = C_Communication::ConvertFloatAmplitudeToUint16(fAmplitude);
    vbias = C_Communication::ConvertFloatBiasToUint16(fBias);
    offset = C_Communication::ConvertFloatOffsetToUint16(fOffset);
    ResetMeasurementBuffer();
}

void loop()
{
    g_Comm.ReadAndProcess();
}

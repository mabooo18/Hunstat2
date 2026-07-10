#include "c_ca.h"
#include "../ad5940/ad5940.h"
#include "../setup/ad5941_setup.h"
#include "../../utilities.h"
#include "../utils/status_utils.h"

extern ADCFilterCfg_Type adc_filter;
extern ADCBaseCfg_Type adc_base;
extern HSLoopCfg_Type HpLoopCfg;
extern uint8_t tia_rf;
extern uint32_t SetDACLevel(float mV);

void C_CA::Begin(C_DataStorage* pData)
{
    m_pData = pData;
}

void C_CA::ConfigDCMeasurement(float voltage_mV)
{
    AD5940_AFECtrlS(AFECTRL_WG, bFALSE);
    WGCfg_Type wgInit;
    wgInit.WgType = WGTYPE_MMR;
    wgInit.GainCalEn = bTRUE;
    wgInit.OffsetCalEn = bFALSE;
    wgInit.WgCode = SetDACLevel(voltage_mV);
    AD5940_WGCfgS(&wgInit);

    HpLoopCfg.SWMatCfg.Dswitch = SWD_CE0;
    HpLoopCfg.SWMatCfg.Pswitch = SWP_RE0;
    HpLoopCfg.SWMatCfg.Nswitch = SWN_SE0;
    HpLoopCfg.SWMatCfg.Tswitch = SWT_TRTIA | SWT_SE0LOAD;
    AD5940_HSLoopCfgS(&HpLoopCfg);

    AD5940_AFECtrlS(AFECTRL_DACREFPWR | AFECTRL_EXTBUFPWR | AFECTRL_INAMPPWR |
                    AFECTRL_HSTIAPWR | AFECTRL_HSDACPWR | AFECTRL_DCBUFPWR, bTRUE);
    AD5940_ADCMuxCfgS(ADCMUXP_HSTIA_P, ADCMUXN_HSTIA_N);

    adc_base.ADCPga = 1;
    AD5940_ADCBaseCfgS(&adc_base);

    adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
    adc_filter.ADCSinc2Osr = ADCSINC2OSR_1333;
    adc_filter.ADCAvgNum = ADCAVGNUM_16;
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = bTRUE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    AD5940_ADCFilterCfgS(&adc_filter);
}

uint32_t C_CA::MeasureCurrentRaw()
{
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV, bTRUE);
    delayMicroseconds(500);
    int32_t time_out = 100;
    uint32_t result = AD5940_TakeMeasurement(AFECTRL_ADCPWR | AFECTRL_ADCCNV,
                                             AFEINTSRC_SINC2RDY,
                                             AFERESULT_SINC2,
                                             &time_out);
    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    return result;
}

float C_CA::RawToCurrent(uint32_t rawCode)
{
    const float Vref_mV = 1820.0f;
    float PGA_G = 1.0f;
    float Rf_Ohm = 200.0f;
    uint32_t rf_values[] = {200, 1000, 5000, 10000, 20000, 40000, 80000, 160000};
    if (tia_rf < 8) Rf_Ohm = rf_values[tia_rf];
    float code = (int16_t)(rawCode & 0xFFFF);
    return (code * Vref_mV / 1000.0f) / (PGA_G * Rf_Ohm * 32768.0f);
}

void C_CA::Run()
{
    Utils_SetStatusLed(CYAN);
    Serial.println("CA_START");
    m_pData->CA_NumSamples = (uint32_t)(m_pData->CA_Duration_s * m_pData->CA_SampleRate_Hz);
    if (m_pData->CA_NumSamples < 1) m_pData->CA_NumSamples = 1;

    ConfigDCMeasurement(m_pData->CA_Voltage_mV);
    for (uint32_t i = 0; i < m_pData->CA_NumSamples; ++i)
    {
        uint32_t raw = MeasureCurrentRaw();
        float current_A = RawToCurrent(raw);
        float time_s = (float)i / m_pData->CA_SampleRate_Hz;
        char buf[100];
        snprintf(buf, sizeof(buf), "CA,%.4f,%.4e", time_s, current_A);
        Serial.println(buf);
        delay((int)(1000.0f / m_pData->CA_SampleRate_Hz));
    }

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV | AFECTRL_WG | AFECTRL_DACREFPWR, bFALSE);
    Utils_SetStatusLed(GREEN);
    Serial.println("CA_END");
}

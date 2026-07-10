#include "c_dpv.h"
#include "../ad5940/ad5940.h"
#include "../setup/ad5941_setup.h"
#include "../../utilities.h"
#include "../utils/status_utils.h"

extern ADCFilterCfg_Type adc_filter;
extern ADCBaseCfg_Type adc_base;
extern HSLoopCfg_Type HpLoopCfg;
extern uint8_t tia_rf;
extern uint32_t SetDACLevel(float mV);

void C_DPV::Begin(C_DataStorage* pData)
{
    m_pData = pData;
}

void C_DPV::ConfigDCMeasurement(float voltage_mV)
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

uint32_t C_DPV::MeasureCurrentRaw()
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

float C_DPV::RawToCurrent(uint32_t rawCode)
{
    const float Vref_mV = 1820.0f;
    float PGA_G = 1.0f;
    float Rf_Ohm = 200.0f;
    uint32_t rf_values[] = {200, 1000, 5000, 10000, 20000, 40000, 80000, 160000};
    if (tia_rf < 8) Rf_Ohm = rf_values[tia_rf];
    float code = (int16_t)(rawCode & 0xFFFF);
    return (code * Vref_mV / 1000.0f) / (PGA_G * Rf_Ohm * 32768.0f);
}

void C_DPV::Run()
{
    Utils_SetStatusLed(MAGENTA);
    Serial.println("DPV_START");

    int numSteps = (int)(fabs(m_pData->DPV_End_mV - m_pData->DPV_Start_mV) / m_pData->DPV_Step_mV) + 1;
    float voltage = m_pData->DPV_Start_mV;
    float stepDirection = (m_pData->DPV_End_mV > m_pData->DPV_Start_mV) ? m_pData->DPV_Step_mV : -m_pData->DPV_Step_mV;
    int pulseDelay_us = (int)(m_pData->DPV_PulseWidth_s * 1e6f);
    int periodDelay_us = (int)(m_pData->DPV_PulsePeriod_s * 1e6f);
    int sampleDelay_us = (int)(m_pData->DPV_SampleDelay_s * 1e6f);

    for (int step = 0; step < numSteps; ++step)
    {
        ConfigDCMeasurement(voltage);
        delayMicroseconds(sampleDelay_us);
        uint32_t raw_base = MeasureCurrentRaw();
        float I_base = RawToCurrent(raw_base);

        float V_pulse = voltage + m_pData->DPV_Amplitude_mV;
        ConfigDCMeasurement(V_pulse);
        delayMicroseconds(sampleDelay_us);
        uint32_t raw_pulse = MeasureCurrentRaw();
        float I_pulse = RawToCurrent(raw_pulse);

        float delta_I = I_pulse - I_base;
        char buf[100];
        snprintf(buf, sizeof(buf), "DPV,%.2f,%.4e", voltage, delta_I);
        Serial.println(buf);

        voltage += stepDirection;
        delayMicroseconds(periodDelay_us - pulseDelay_us);
    }

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV | AFECTRL_WG | AFECTRL_DACREFPWR, bFALSE);
    Utils_SetStatusLed(GREEN);
    Serial.println("DPV_END");
}

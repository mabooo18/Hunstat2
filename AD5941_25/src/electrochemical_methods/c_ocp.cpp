#include "c_ocp.h"
#include "../ad5940/ad5940.h"
#include "../ad5940/debug.h"
#include "../setup/ad5941_setup.h"
#include "../../utilities.h"
#include "../../src/interface/led_interface.h"
#include "../../src/utils/status_utils.h"

extern uint32_t AD5940_TakeMeasurement(uint32_t afectrl_bits, uint32_t afectrl_readybit, uint32_t afectrl_resultType, int32_t *time_out);
extern uint32_t SetDACLevel(float mV);
extern ADCFilterCfg_Type adc_filter;
extern ADCBaseCfg_Type adc_base;
extern HSLoopCfg_Type HpLoopCfg;
extern uint32_t ADCCON;
extern uint32_t HSDACDAT;
extern uint32_t OCP_sum;
extern bool SeeedStatMode;
extern float WEmV;
extern float WEfrom;
extern float WEto;
extern float WEstep;
extern BoolFlag ocpCalibration;
extern BoolFlag ocpCalibrationCycling;
extern float constA;
extern float constB;
extern bool useConstAB;
extern uint16_t OCP_npts;
extern uint8_t vzero;
extern uint16_t vbias;
extern uint16_t amplitude;
extern uint16_t offset;
extern uint16_t adc_delay_ms;
extern uint16_t settling_delay_ms;
extern uint32_t settling_parameter;
extern float fRcal;
extern float fAmplitude;
extern float fBias;
extern float fOffset;
extern uint8_t pga_gain;
extern uint8_t tia_rf;
extern uint8_t EIS_mode;
extern uint16_t nfreqs;
extern uint32_t freqlo;
extern uint32_t freqhi;
extern uint32_t _CGmax;
extern uint32_t _CGmin;

static void ConfigureHSLoopCfg(uint32_t hsDacDat)
{
    HSLoopCfg_Type loopCfg;
    AD5940_StructInit(&loopCfg, sizeof(HSLoopCfg_Type));
    loopCfg.HsDacCfg.ExcitBufGain = EXCITBUFGAIN_2;
    loopCfg.HsDacCfg.HsDacGain = HSDACGAIN_1;
    loopCfg.HsDacCfg.HsDacUpdateRate = 7;
    loopCfg.HsTiaCfg.DiodeClose = bFALSE;
    loopCfg.HsTiaCfg.HstiaBias = HSTIABIAS_1P1;
    loopCfg.HsTiaCfg.HstiaCtia = 16;
    loopCfg.HsTiaCfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    loopCfg.HsTiaCfg.HstiaDeRtia = HSTIADERTIA_TODE;
    loopCfg.HsTiaCfg.HstiaRtiaSel = HSTIARTIA_200;
    loopCfg.SWMatCfg.Dswitch = SWD_CE0;
    loopCfg.SWMatCfg.Pswitch = SWP_CE0;
    loopCfg.SWMatCfg.Nswitch = SWN_SE0LOAD;
    loopCfg.SWMatCfg.Tswitch = SWT_TRTIA | SWT_SE0LOAD;
    loopCfg.WgCfg.WgType = WGTYPE_MMR;
    loopCfg.WgCfg.GainCalEn = bFALSE;
    loopCfg.WgCfg.OffsetCalEn = bFALSE;
    loopCfg.WgCfg.WgCode = hsDacDat;
    AD5940_HSLoopCfgS(&loopCfg);
}

void C_OCP::Begin(C_DataStorage* pData)
{
    m_pData = pData;
}

void C_OCP::Configure()
{
    AD5940_PGA_Calibration();
    AD5940_AFEPwrBW(AFEPWR_LP, AFEBW_250KHZ);

    adc_base.ADCPga = ADCPGA_1P5;
    AD5940_ADCBaseCfgS(&adc_base);

    adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
    adc_filter.ADCSinc2Osr = ADCSINC2OSR_1333;
    adc_filter.ADCAvgNum = ADCAVGNUM_2;
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = bTRUE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    AD5940_ADCFilterCfgS(&adc_filter);

    AD5940_ADCMuxCfgS(ADCMUXP_VSE0, ADCMUXN_AIN3);

    AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

    OutputPulse(D4, 300);
    if (m_pData->OCP_Calibration == bTRUE)
    {
        digitalWrite(D4, HIGH);
        uint32_t hsDacDat = SetDACLevel(m_pData->WE_mV);
        digitalWrite(D4, LOW);
        ConfigureHSLoopCfg(hsDacDat);
    }

    BoolFlag enable = m_pData->OCP_Calibration ? bTRUE : bFALSE;
    AD5940_AFECtrlS(AFECTRL_DACREFPWR, enable);
    AD5940_AFECtrlS(AFECTRL_EXTBUFPWR | AFECTRL_INAMPPWR | AFECTRL_HSTIAPWR | AFECTRL_HSDACPWR, enable);
    AD5940_AFECtrlS(AFECTRL_WG, enable);
    AD5940_AFECtrlS(AFECTRL_DCBUFPWR, enable);
}

uint32_t C_OCP::Do1Measurement()
{
    digitalWrite(D5, HIGH);
    int32_t time_out = 1000;
    uint32_t result = AD5940_TakeMeasurement(AFECTRL_ADCPWR | AFECTRL_ADCCNV,
                                             AFEINTSRC_SINC2RDY,
                                             AFERESULT_SINC2,
                                             &time_out);
    digitalWrite(D5, LOW);
    return result;
}

void C_OCP::Measure()
{
    Utils_SetStatusLed(MAGENTA);

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV, bTRUE);
    delay(5);

    digitalWrite(D4, HIGH);
    if (m_pData->OCP_CalibrationCycling == bTRUE)
    {
        float step = m_pData->WEFrom_mV < m_pData->WETo_mV ? abs(m_pData->WEStep_mV) : -abs(m_pData->WEStep_mV);
        for (float we = m_pData->WEFrom_mV; m_pData->WEFrom_mV < m_pData->WETo_mV ? we <= m_pData->WETo_mV : we >= m_pData->WETo_mV; we += step)
        {
            delayMicroseconds(500);
            digitalWrite(D4, HIGH);
            SetDACLevel(we);

            uint32_t ocp_sum = 0;
            for (uint16_t i = 0; i < m_pData->OCP_Npts; ++i)
            {
                ocp_sum += Do1Measurement();
            }

            m_pData->OCP_Sum = ocp_sum;
            digitalWrite(D4, LOW);
        }
    }
    else
    {
        m_pData->OCP_Sum = 0;
        for (uint16_t i = 0; i < m_pData->OCP_Npts; ++i)
        {
            delayMicroseconds(500);
            m_pData->OCP_Sum += Do1Measurement();
        }
    }

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_ADCCNV, bFALSE);
    digitalWrite(D4, LOW);

    m_pData->ADCCON = AD5940_ReadReg(REG_AFE_ADCCON);
    if (m_pData->OCP_CalibrationCycling != bTRUE)
    {
        Serial.print('Z');
    }

    m_pData->OCP_CalibrationCycling = false;
    m_pData->SeeedStatMode = false;
    Utils_SetStatusLed(GREEN);
}

float C_OCP::Calculate()
{
    float ocp_mV;
    float ocp_1 = static_cast<float>(m_pData->OCP_Sum) / m_pData->OCP_Npts;
    if (m_pData->SeeedStatMode)
    {
        ocp_1 *= -1.0f;
    }

    if (m_pData->UseConstAB)
    {
        ocp_mV = (ocp_1 - m_pData->ConstA) / m_pData->ConstB;
    }
    else
    {
        uint8_t GNPGA = (m_pData->ADCCON & BITM_AFE_ADCCON_GNPGA) >> BITP_AFE_ADCCON_GNPGA;
        float Vref_mV = (GNPGA == 1) ? 1835.0f : 1820.0f;
        float PGA_G_values[] = { 1.0f, 1.5f, 2.0f, 4.0f, 9.0f, 9.0f };
        float PGA_G = PGA_G_values[GNPGA];
        uint8_t MUXSELP = (m_pData->ADCCON & BITM_AFE_ADCCON_MUXSELP) >> BITP_AFE_ADCCON_MUXSELP;
        uint8_t MUXSELN = (m_pData->ADCCON & BITM_AFE_ADCCON_MUXSELN) >> BITP_AFE_ADCCON_MUXSELN;
        const uint8_t MUXSELN_VBIAS_CAP = 8;
        float VBIAS_CAP_mV = (MUXSELN == MUXSELN_VBIAS_CAP) ? 1110.0f : 0.0f;
        const float ADC_MIDDLE_AND_ACTIVE_RANGE = 1 << 15;
        ocp_mV = (ocp_1 - ADC_MIDDLE_AND_ACTIVE_RANGE) * (Vref_mV / PGA_G) / ADC_MIDDLE_AND_ACTIVE_RANGE + VBIAS_CAP_mV;
    }

    return ocp_mV;
}

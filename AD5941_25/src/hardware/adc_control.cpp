#include "adc_control.h"

extern ADCFilterCfg_Type adc_filter;
extern DFTCfg_Type DftCfg;
extern FIFOCfg_Type fifo_cfg;
extern ADCBaseCfg_Type adc_base;

void Hardware_Init_AD5940_ADC(float freq)
{
    fifo_cfg.FIFOEn = bFALSE;
    fifo_cfg.FIFOMode = FIFOMODE_FIFO;
    fifo_cfg.FIFOSize = FIFOSIZE_4KB;
    fifo_cfg.FIFOSrc = FIFOSRC_DFT;
    fifo_cfg.FIFOThresh = 2;
    AD5940_FIFOCfg(&fifo_cfg);

    fifo_cfg.FIFOEn = bTRUE;
    AD5940_FIFOCfg(&fifo_cfg);

    AD5940_ADCMuxCfgS(ADCMUXP_HSTIA_P, ADCMUXN_HSTIA_N);

    AD5940_StructInit(&adc_filter, sizeof(adc_filter));
    AD5940_StructInit(&DftCfg, sizeof(DftCfg));

    if (freq < .11) {
        adc_filter.ADCAvgNum = ADCAVGNUM_16;
        adc_filter.ADCSinc2Osr = ADCSINC2OSR_1067;
        adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
        adc_filter.BpNotch = bTRUE;
        adc_filter.BpSinc3 = bFALSE;
        adc_filter.Sinc2NotchEnable = bTRUE;
        adc_filter.ADCRate = ADCRATE_800KHZ;

        DftCfg.DftNum = DFTNUM_16384;
        DftCfg.DftSrc = DFTSRC_SINC2NOTCH;
        DftCfg.HanWinEn = bTRUE;
    }
    else if (freq < .51) {
        adc_filter.ADCAvgNum = ADCAVGNUM_16;
        adc_filter.ADCSinc2Osr = ADCSINC2OSR_267;
        adc_filter.ADCSinc3Osr = ADCSINC3OSR_5;
        adc_filter.BpNotch = bTRUE;
        adc_filter.BpSinc3 = bFALSE;
        adc_filter.Sinc2NotchEnable = bTRUE;
        adc_filter.ADCRate = ADCRATE_800KHZ;

        DftCfg.DftNum = DFTNUM_8192;
        DftCfg.DftSrc = DFTSRC_SINC2NOTCH;
        DftCfg.HanWinEn = bTRUE;
    }
    else if (freq < 5) {
        adc_filter.ADCAvgNum = ADCAVGNUM_16;
        adc_filter.ADCSinc2Osr = ADCSINC2OSR_178;
        adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
        adc_filter.BpNotch = bTRUE;
        adc_filter.BpSinc3 = bFALSE;
        adc_filter.Sinc2NotchEnable = bTRUE;
        adc_filter.ADCRate = ADCRATE_800KHZ;

        DftCfg.DftNum = DFTNUM_8192;
        DftCfg.DftSrc = DFTSRC_SINC2NOTCH;
        DftCfg.HanWinEn = bTRUE;
    }
    else if (freq < 450) {
        adc_filter.ADCAvgNum = ADCAVGNUM_16;
        adc_filter.ADCSinc2Osr = ADCSINC2OSR_44;
        adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
        adc_filter.BpNotch = bTRUE;
        adc_filter.BpSinc3 = bFALSE;
        adc_filter.Sinc2NotchEnable = bTRUE;
        adc_filter.ADCRate = ADCRATE_800KHZ;

        DftCfg.DftNum = DFTNUM_4096;
        DftCfg.DftSrc = DFTSRC_SINC2NOTCH;
        DftCfg.HanWinEn = bTRUE;
    }
    else if (freq < 80000) {
        adc_filter.ADCAvgNum = ADCAVGNUM_16;
        adc_filter.ADCSinc2Osr = ADCSINC2OSR_178;
        adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
        adc_filter.BpNotch = bTRUE;
        adc_filter.BpSinc3 = bFALSE;
        adc_filter.Sinc2NotchEnable = bFALSE;
        adc_filter.ADCRate = ADCRATE_800KHZ;

        DftCfg.DftNum = DFTNUM_16384;
        DftCfg.DftSrc = DFTSRC_SINC3;
        DftCfg.HanWinEn = bTRUE;
    }
    else {
        adc_filter.ADCAvgNum = ADCAVGNUM_16;
        adc_filter.ADCSinc2Osr = ADCSINC2OSR_178;
        adc_filter.ADCSinc3Osr = ADCSINC3OSR_2;
        adc_filter.BpNotch = bTRUE;
        adc_filter.BpSinc3 = bFALSE;
        adc_filter.Sinc2NotchEnable = bFALSE;
        adc_filter.ADCRate = ADCRATE_800KHZ;

        DftCfg.DftNum = DFTNUM_16384;
        DftCfg.DftSrc = DFTSRC_SINC3;
        DftCfg.HanWinEn = bTRUE;
    }

    AD5940_ADCFilterCfgS(&adc_filter);
    AD5940_DFTCfgS(&DftCfg);
    AD5940_ADCBaseCfgS(&adc_base);
}

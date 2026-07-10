#include "wave_gen.h"
#include "gain_control.h"

extern uint32_t _CGmax;
extern uint32_t _CGmin;
extern bool use_variable_gain;
extern uint8_t pga_gain;
extern uint8_t tia_rf;
extern uint32_t SYS_CLOCK_HZ;
extern bool SeeedStatMode;
extern HSLoopCfg_Type HpLoopCfg;
extern ADCBaseCfg_Type adc_base;
extern CLKCfg_Type clk_cfg;
extern AFERefCfg_Type aferef_cfg;
extern uint16_t offset;
extern uint8_t EIS_mode;
extern uint16_t amplitude;
extern uint16_t vbias;
extern uint16_t frequency;
extern uint32_t verbose;

void Hardware_Do_WaveGen(uint8_t mode, float frequency, uint16_t amplitude, uint8_t tia_code)
{
    if (SeeedStatMode) {
        digitalWrite(D4, HIGH);
    }

    uint8_t _TIA_Rf, _PGA_gain;
    float closest_CG;

    clk_cfg.ADCClkDiv = ADCCLKDIV_1;
    clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
    clk_cfg.SysClkDiv = SYSCLKDIV_1;
    clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
    clk_cfg.HfOSC32MHzMode = bFALSE;
    clk_cfg.HFOSCEn = bTRUE;
    clk_cfg.HFXTALEn = bFALSE;
    clk_cfg.LFOSCEn = bTRUE;
    AD5940_CLKCfg(&clk_cfg);

    aferef_cfg.HpBandgapEn = bTRUE;
    aferef_cfg.Hp1V1BuffEn = bTRUE;
    aferef_cfg.Hp1V8BuffEn = bTRUE;
    aferef_cfg.Disc1V1Cap = bFALSE;
    aferef_cfg.Disc1V8Cap = bFALSE;
    aferef_cfg.Hp1V8ThemBuff = bFALSE;
    aferef_cfg.Hp1V8Ilimit = bFALSE;
    aferef_cfg.Lp1V1BuffEn = bFALSE;
    aferef_cfg.Lp1V8BuffEn = bFALSE;
    aferef_cfg.LpBandgapEn = bTRUE;
    aferef_cfg.LpRefBufEn = bTRUE;
    aferef_cfg.LpRefBoostEn = bFALSE;
    AD5940_REFCfgS(&aferef_cfg);

    HpLoopCfg.HsTiaCfg.DiodeClose = bFALSE;
    HpLoopCfg.HsTiaCfg.HstiaBias = HSTIABIAS_1P1;
    HpLoopCfg.HsTiaCfg.HstiaCtia = 16;
    HpLoopCfg.HsTiaCfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    HpLoopCfg.HsTiaCfg.HstiaDeRtia = HSTIADERTIA_TODE;

    if (use_variable_gain) {
        Hardware_FindOptimum_Rf_PGA(_CGmax, _CGmin, frequency, &_TIA_Rf, &_PGA_gain, &closest_CG);
        HpLoopCfg.HsTiaCfg.HstiaRtiaSel = _TIA_Rf;
        adc_base.ADCPga = _PGA_gain;
    }
    else {
        HpLoopCfg.HsTiaCfg.HstiaRtiaSel = tia_code;
        adc_base.ADCPga = pga_gain;
    }

    AD5940_ADCBaseCfgS(&adc_base);

    if (mode == 0) {
        HpLoopCfg.SWMatCfg.Dswitch = SWD_CE0;
        HpLoopCfg.SWMatCfg.Pswitch = SWP_RE0;
        HpLoopCfg.SWMatCfg.Nswitch = SWN_SE0;
        HpLoopCfg.SWMatCfg.Tswitch = SWT_TRTIA | SWT_SE0LOAD;
    }
    else {
        HpLoopCfg.SWMatCfg.Dswitch = SWD_RCAL0;
        HpLoopCfg.SWMatCfg.Pswitch = SWP_RCAL0;
        HpLoopCfg.SWMatCfg.Nswitch = SWN_RCAL1;
        HpLoopCfg.SWMatCfg.Tswitch = SWT_TRTIA | SWT_RCAL1;
    }

    AD5940_AFECtrlS(AFECTRL_WG, bFALSE);

    HpLoopCfg.WgCfg.WgType = WGTYPE_SIN;
    HpLoopCfg.WgCfg.GainCalEn = bFALSE;
    HpLoopCfg.WgCfg.OffsetCalEn = bFALSE;
    HpLoopCfg.WgCfg.SinCfg.SinFreqWord = AD5940_WGFreqWordCal(frequency, SYS_CLOCK_HZ);
    HpLoopCfg.WgCfg.SinCfg.SinAmplitudeWord = amplitude;
    HpLoopCfg.WgCfg.SinCfg.SinOffsetWord = offset;
    HpLoopCfg.WgCfg.SinCfg.SinPhaseWord = 0;
    AD5940_HSLoopCfgS(&HpLoopCfg);

    AD5940_AFECtrlS(AFECTRL_DACREFPWR, bTRUE);
    AD5940_AFECtrlS(AFECTRL_EXTBUFPWR | AFECTRL_INAMPPWR | AFECTRL_HSTIAPWR | AFECTRL_HSDACPWR, bTRUE);
    AD5940_AFECtrlS(AFECTRL_WG, bTRUE);
    AD5940_AFECtrlS(AFECTRL_DCBUFPWR, bTRUE);
    AD5940_AFEPwrBW(AFEPWR_LP, AFEBW_250KHZ);

    if (SeeedStatMode) {
        digitalWrite(D4, LOW);
    }
}

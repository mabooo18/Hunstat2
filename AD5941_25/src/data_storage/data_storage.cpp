/******************************************************************************
 * @file:    data_storage.cpp
 * @brief:   Implementasi C_DataStorage — inisialisasi semua parameter
 *****************************************************************************/

#include "data_storage.h"

void C_DataStorage::Begin() {

    // System state
    SystemStatus            = HUNSTAT_WAITING;
    SeeedStatMode           = false;
    Verbose                 = 0;

    // EIS parameters
    PGA_Gain                = DEFAULT_PGA_GAIN;
    TIA_Rf                  = DEFAULT_TIA_RF;
    EIS_Mode                = MODE_EIS_RZ;
    NFreqs                  = DEFAULT_NFREQS;
    Amplitude               = DEFAULT_AMPLITUDE;
    VBias                   = DEFAULT_VBIAS;
    VZero                   = DEFAULT_VZERO;
    Offset                  = DEFAULT_OFFSET;
    FreqLo                  = 0;
    FreqHi                  = 0;
    CGMax                   = DEFAULT_CGMAX;
    CGMin                   = DEFAULT_CGMIN;
    UseVariableGain         = 0;
    fRcal                   = DEFAULT_RCAL;
    fAmplitude              = DEFAULT_FAMPLITUDE;
    fBias                   = DEFAULT_FBIAS;
    fOffset                 = DEFAULT_FOFFSET;

    // EIS result buffer
    pMeasurement            = Measurements;
    NumberOfMeasurements    = 0;
    memset(Measurements, 0, sizeof(Measurements));

    // OCP parameters
    OCP_Npts                = DEFAULT_OCP_NPTS;
    OCP_Sum                 = 0;
    ADCCON                  = 0;
    ConstA                  = DEFAULT_CONST_A;
    ConstB                  = DEFAULT_CONST_B;
    UseConstAB              = false;
    WE_mV                   = 0.0f;
    WEFrom_mV               = 0.0f;
    WETo_mV                 = 0.0f;
    WEStep_mV               = 0.0f;
    OCP_Calibration         = false;
    OCP_CalibrationCycling  = false;
    HSDACDAT                = 0;

    // CV parameters
    V_Start                 = 0.0f;
    V_Stop                  = 0.0f;
    EStep                   = 0.0f;
    ScanRate                = 0.0f;
    CycleNumber             = 1;

    // CA parameters
    CA_Voltage_mV           = DEFAULT_CA_VOLTAGE_MV;
    CA_Duration_s           = DEFAULT_CA_DURATION_S;
    CA_SampleRate_Hz        = DEFAULT_CA_SAMPLERATE;
    CA_NumSamples           = 0;

    // SWV parameters
    SWV_Start_mV            = DEFAULT_SWV_START;
    SWV_End_mV              = DEFAULT_SWV_END;
    SWV_Step_mV             = DEFAULT_SWV_STEP;
    SWV_Amplitude_mV        = DEFAULT_SWV_AMPLITUDE;
    SWV_Frequency_Hz        = DEFAULT_SWV_FREQUENCY;
    SWV_SampleDelay_s       = DEFAULT_SWV_SAMPLEDELAY;

    // DPV parameters
    DPV_Start_mV            = DEFAULT_DPV_START;
    DPV_End_mV              = DEFAULT_DPV_END;
    DPV_Step_mV             = DEFAULT_DPV_STEP;
    DPV_Amplitude_mV        = DEFAULT_DPV_AMPLITUDE;
    DPV_PulseWidth_s        = DEFAULT_DPV_PULSEWIDTH;
    DPV_PulsePeriod_s       = DEFAULT_DPV_PULSEPERIOD;
    DPV_SampleDelay_s       = DEFAULT_DPV_SAMPLEDELAY;
}
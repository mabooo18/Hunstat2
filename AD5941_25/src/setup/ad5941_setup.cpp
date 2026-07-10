#include "ad5941_setup.h"
#include "../interface/led_interface.h"
#include "../../utilities.h"

extern Adafruit_NeoPixel pixels;

void C_AD5941_Setup::Begin()
{
    pixels.begin();
    pinMode(NEOPIXEL_POWER_PIN, OUTPUT);
    digitalWrite(NEOPIXEL_POWER_PIN, HIGH);

    pinMode(DEBUG_PIN_A, OUTPUT);
    digitalWrite(DEBUG_PIN_A, LOW);
    pinMode(DEBUG_PIN_B, OUTPUT);
    digitalWrite(DEBUG_PIN_B, LOW);

    AD5941_InitAll_Standalone();

    Interface_SetLed(RED);
    delay(250);
    AD5940_PGA_Calibration_Standalone();
    delay(250);
    Interface_SetLed(GREEN);
    Interface_SetPixelsColor(0, 255, 0);
}

void C_AD5941_Setup::InitAll()
{
    AD5941_InitAll_Standalone();
}

void C_AD5941_Setup::PGACalibration()
{
    AD5940_PGA_Calibration_Standalone();
}

Adafruit_NeoPixel* C_AD5941_Setup::GetPixels()
{
    return &m_Pixels;
}

void AD5941_InitAll_Standalone()
{
    AD5940_HWReset();
    AD5940_MCUResourceInit(0);
    AD5940_Initialize();
}

void AD5940_PGA_Calibration_Standalone()
{
    AD5940Err err;
    ADCPGACal_Type pgacal;
    pgacal.AdcClkFreq = 16e6;
    pgacal.ADCSinc2Osr = ADCSINC2OSR_1333;
    pgacal.ADCSinc3Osr = ADCSINC3OSR_4;
    pgacal.SysClkFreq = 16e6;
    pgacal.TimeOut10us = 1000;
    pgacal.VRef1p11 = 1.11f;
    pgacal.VRef1p82 = 1.82f;
    pgacal.PGACalType = PGACALTYPE_OFFSETGAIN;
    pgacal.ADCPga = 1;
    err = AD5940_ADCPGACal(&pgacal);
    if (err != AD5940ERR_OK) {
        Serial.println("AD5940 PGA calibration failed.");
    }
}

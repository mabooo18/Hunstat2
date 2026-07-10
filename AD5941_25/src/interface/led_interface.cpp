#include "led_interface.h"

void Interface_SetLed(uint8_t color)
{
    pixels.clear();
    pixels.setPixelColor(0, pixels.Color(255, 255, 255));
    pixels.show();
}

void Interface_SetPixelsColor(uint8_t red, uint8_t green, uint8_t blue)
{
    pixels.clear();
    pixels.setPixelColor(0, pixels.Color(red, green, blue));
    pixels.show();
}

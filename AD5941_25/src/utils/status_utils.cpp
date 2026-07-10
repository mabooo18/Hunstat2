#include "status_utils.h"
#include "../interface/led_interface.h"

void Utils_SetStatusLed(uint8_t color)
{
    Interface_SetLed(color);
}

void Utils_SetStatusPixels(uint8_t red, uint8_t green, uint8_t blue)
{
    Interface_SetPixelsColor(red, green, blue);
}

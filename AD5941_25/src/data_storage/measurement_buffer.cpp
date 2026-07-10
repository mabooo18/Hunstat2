#include "measurement_buffer.h"

#define MEASUREMENT_BUFFER_SIZE 4 * 7 * 20

float Measurements[MEASUREMENT_BUFFER_SIZE];
float* pMeasurement = Measurements;
int numberOfMeasurements = 0;

void ResetMeasurementBuffer()
{
    pMeasurement = Measurements;
    numberOfMeasurements = 0;
}

void AppendMeasurement(float real, float imag)
{
    if (numberOfMeasurements < MEASUREMENT_BUFFER_SIZE / 2)
    {
        *pMeasurement++ = real;
        *pMeasurement++ = imag;
        ++numberOfMeasurements;
    }
}

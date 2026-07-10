#include "gain_control.h"

void Hardware_FindOptimum_Rf_PGA(uint32_t CGmax, uint32_t CGmin, float freq, uint8_t* TIA_Rf, uint8_t* PGA_gain, float* closest_CG)
{
    uint32_t rf_values[] = {200, 1000, 5000, 10000, 20000, 40000, 80000, 160000};
    float pga_values[] = {1.0, 1.5, 2, 4, 9};

    float logmax, logmin, logf, logCG, m, c, CG, target_CG, best_error, output_CG, error;

    logf = log10(freq);
    logmax = log10(CGmax);
    logmin = log10(CGmin);

    m = (logmin - logmax) / 6.0;
    c = (5 * logmax + logmin) / 6.0;

    logCG = m * logf + c;
    CG = pow(10, logCG);

    target_CG = CG;
    best_error = 1000000.0;
    *closest_CG = 0.0;

    for (uint8_t i = 0; i < 8; i++) {
        for (uint8_t j = 0; j < 5; j++) {
            output_CG = rf_values[i] * pga_values[j];
            error = abs(target_CG - output_CG);
            if (error < best_error) {
                best_error = error;
                *TIA_Rf = i;
                *PGA_gain = j;
                *closest_CG = output_CG;
            }
        }
    }
}

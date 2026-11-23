/*
 * dsp.h
 *
 *  Created on: 22-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#ifndef DSP_H_
#define DSP_H_

#include <stdint.h>

#define DSP_FFT_SIZE     256U
#define DSP_SAMPLE_RATE  10000.0f
#define DSP_ADC_VREF     3.3f
#define DSP_ADC_MAX      4096.0f
#define DSP_MAX_HARMONICS 5U

typedef struct {
    float    fundamental_hz;
    float    harmonics_hz[DSP_MAX_HARMONICS];
    uint8_t  num_harmonics;
} dsp_harmonics_t;

void dsp_init(void);

float dsp_calc_rms(const uint16_t *samples);

float dsp_calc_dominant_freq(const uint16_t *samples);

void dsp_calc_harmonics(const uint16_t *samples, dsp_harmonics_t *out);

#endif /* DSP_H_ */


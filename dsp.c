/*
 * dsp.c
 *
 *  Created on: 22-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#include "dsp.h"
#include <math.h>

#define HALF_N  (DSP_FFT_SIZE / 2U)
#define BIN_HZ  (DSP_SAMPLE_RATE / (float)DSP_FFT_SIZE) // 39.0625 Hz
#define PI_F    3.14159265358979323846f

static float cos_table[HALF_N];
static float sin_table[HALF_N];
static float fft_real[DSP_FFT_SIZE];
static float fft_imag[DSP_FFT_SIZE];
static float fft_mag[HALF_N];

void dsp_init(void) {
    uint32_t k;
    for (k = 0; k < HALF_N; k++) {
        float angle = (2.0f * PI_F * (float)k) / (float)DSP_FFT_SIZE;
        cos_table[k] = cosf(angle);
        sin_table[k] = sinf(angle);
    }
}

static void load_and_strip_dc(const uint16_t *samples) {
    uint32_t i;
    float sum = 0.0f;
    const float scale = DSP_ADC_VREF / DSP_ADC_MAX;

    for (i = 0; i < DSP_FFT_SIZE; i++) {
        float v = (float)samples[i] * scale;
        fft_real[i] = v;
        sum += v;
    }

    float mean = sum / (float)DSP_FFT_SIZE;
    for (i = 0; i < DSP_FFT_SIZE; i++) {
        fft_real[i] -= mean;
        fft_imag[i] = 0.0f;
    }
}

static void run_fft_and_mag(void) {
    uint32_t i, j, k;

    // 1. Bit-reversal permutation
    j = 0;
    for (i = 0; i < DSP_FFT_SIZE; i++) {
        if (i < j) {
            float temp = fft_real[i];
            fft_real[i] = fft_real[j];
            fft_real[j] = temp;
        }
        uint32_t bit = DSP_FFT_SIZE >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j |= bit;
    }

    // 2. Butterfly stages
    uint32_t stage;
    for (stage = 1; stage <= 8; stage++) {
        uint32_t half_size = 1U << (stage - 1U);
        uint32_t step = 1U << stage;
        uint32_t twiddle_step = DSP_FFT_SIZE >> stage;

        for (k = 0; k < half_size; k++) {
            uint32_t tw_idx = k * twiddle_step;
            float wr = cos_table[tw_idx];
            float wi = -sin_table[tw_idx];

            for (i = k; i < DSP_FFT_SIZE; i += step) {
                j = i + half_size;
                float tr = wr * fft_real[j] - wi * fft_imag[j];
                float ti = wr * fft_imag[j] + wi * fft_real[j];

                fft_real[j] = fft_real[i] - tr;
                fft_imag[j] = fft_imag[i] - ti;
                fft_real[i] += tr;
                fft_imag[i] += ti;
            }
        }
    }

    // 3. Magnitude spectrum (bins 1..127)
    fft_mag[0] = 0.0f;
    for (k = 1; k < HALF_N; k++) {
        fft_mag[k] = sqrtf((fft_real[k] * fft_real[k]) + (fft_imag[k] * fft_imag[k]));
    }
}

static uint32_t find_fundamental_bin(float *out_peak_mag) {
    uint32_t k;
    uint32_t max_bin = 1;
    float max_val = fft_mag[1];

    for (k = 2; k < HALF_N; k++) {
        if (fft_mag[k] > max_val) {
            max_val = fft_mag[k];
            max_bin = k;
        }
    }

    if (out_peak_mag) {
        *out_peak_mag = max_val;
    }
    return max_bin;
}

float dsp_calc_rms(const uint16_t *samples) {
    load_and_strip_dc(samples);

    uint32_t i;
    float sum_sq = 0.0f;
    for (i = 0; i < DSP_FFT_SIZE; i++) {
        sum_sq += fft_real[i] * fft_real[i];
    }

    return sqrtf(sum_sq / (float)DSP_FFT_SIZE);
}

float dsp_calc_dominant_freq(const uint16_t *samples) {
    load_and_strip_dc(samples);
    run_fft_and_mag();

    float peak_mag = 0.0f;
    uint32_t k1 = find_fundamental_bin(&peak_mag);

    // Return 0 Hz if input is flat silence/noise
    if (peak_mag < 0.5f) {
        return 0.0f;
    }

    return (float)k1 * BIN_HZ;
}

void dsp_calc_harmonics(const uint16_t *samples, dsp_harmonics_t *out) {
    out->fundamental_hz = 0.0f;
    out->num_harmonics = 0;

    load_and_strip_dc(samples);
    run_fft_and_mag();

    float fund_mag = 0.0f;
    uint32_t k1 = find_fundamental_bin(&fund_mag);

    if (fund_mag < 0.5f) {
        return;
    }

    out->fundamental_hz = (float)k1 * BIN_HZ;

    // Only count a harmonic if it is at least 5% of the fundamental amplitude
    float threshold = fund_mag * 0.05f;
    uint32_t n;

    for (n = 2; n <= (DSP_MAX_HARMONICS + 1U); n++) {
        uint32_t expected_bin = n * k1;
        if (expected_bin >= HALF_N) {
            break; // Exceeded 5 kHz Nyquist limit
        }

        // Check expected_bin +/- 1 to handle frequencies between bin centers
        uint32_t start = (expected_bin > 1U) ? (expected_bin - 1U) : 1U;
        uint32_t end   = ((expected_bin + 1U) < HALF_N) ? (expected_bin + 1U) : (HALF_N - 1U);

        uint32_t best_bin = start;
        uint32_t b;
        for (b = start + 1U; b <= end; b++) {
            if (fft_mag[b] > fft_mag[best_bin]) {
                best_bin = b;
            }
        }

        if (fft_mag[best_bin] >= threshold) {
            out->harmonics_hz[out->num_harmonics++] = (float)best_bin * BIN_HZ;
        }
    }
}

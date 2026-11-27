/*
 * dsp_task.c
 *
 *  Created on: 24-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#include "tasks.h"
#include "adc.h"

void dsp_task(void *arg) {
    (void)arg;

    cmd_type_t active_metric = METRIC_NONE;
    uint8_t continuous = 0;

    while (1) {
        uint16_t *buf = adc0_get();

        cmd_type_t incoming;
        if (mq_receive(&cmd_queue, &incoming, 0) == OS_OK) {
            if (incoming == CMD_TOGGLE_CONT) {
                continuous = !continuous;
            } else {
                active_metric = incoming;
            }
        }

        if (active_metric != METRIC_NONE) {
            float scalar_res = 0.0f;
            dsp_harmonics_t harm_res;

            if (active_metric == METRIC_RMS) {
                scalar_res = dsp_calc_rms(buf);
            } else if (active_metric == METRIC_DOM_FREQ) {
                scalar_res = dsp_calc_dominant_freq(buf);
            } else if (active_metric == METRIC_HARMONICS) {
                dsp_calc_harmonics(buf, &harm_res);
            }

            res_acquire(RES_RESULTS, OS_FOREVER);
            g_results.metric = active_metric;
            g_results.value = scalar_res;
            if (active_metric == METRIC_HARMONICS) {
                g_results.harmonics = harm_res;
            }
            g_results.overrun_cnt = adc0->overrun;
            res_release(RES_RESULTS);

            sem_post(&results_sem);

            if (!continuous) {
                active_metric = METRIC_NONE;
            }
        }
    }
}

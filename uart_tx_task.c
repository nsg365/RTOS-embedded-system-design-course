/*
 * uart_tx_task.c
 *
 *  Created on: 24-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#include "tasks.h"
#include "uart.h"
#include <stdio.h>

// Prints positive float to 2 decimal places without requiring full float printf support
static void format_fixed2(char *dst, size_t max_len, const char *prefix, float val, const char *suffix) {
    if (val < 0.0f) {
        val = 0.0f;
    }
    uint32_t scaled = (uint32_t)(val * 100.0f + 0.5f);
    uint32_t whole  = scaled / 100U;
    uint32_t frac   = scaled % 100U;
    snprintf(dst, max_len, "%s%lu.%02lu%s", prefix, (unsigned long)whole, (unsigned long)frac, suffix);
}

void uart_tx_task(void *arg) {
    (void)arg;

    shared_results_t local_res;
    uint32_t last_overrun = 0;
    char line[64];

    while (1) {
        sem_wait(&results_sem, OS_FOREVER);

        res_acquire(RES_RESULTS, OS_FOREVER);
        local_res = g_results;
        res_release(RES_RESULTS);

        if (local_res.metric == METRIC_RMS) {
            format_fixed2(line, sizeof(line), "RMS: ", local_res.value, " V\r\n");
            uart_send_string(uart0, line);
        } else if (local_res.metric == METRIC_DOM_FREQ) {
            format_fixed2(line, sizeof(line), "FREQ: ", local_res.value, " Hz\r\n");
            uart_send_string(uart0, line);
        } else if (local_res.metric == METRIC_HARMONICS) {
            format_fixed2(line, sizeof(line), "F1: ", local_res.harmonics.fundamental_hz, " Hz");
            uart_send_string(uart0, line);

            uint8_t i;
            for (i = 0; i < local_res.harmonics.num_harmonics; i++) {
                snprintf(line, sizeof(line), " | H%u: ", (unsigned int)(i + 2U));
                uart_send_string(uart0, line);
                format_fixed2(line, sizeof(line), "", local_res.harmonics.harmonics_hz[i], " Hz");
                uart_send_string(uart0, line);
            }
            uart_send_string(uart0, "\r\n");
        }

        if (local_res.overrun_cnt > last_overrun) {
            snprintf(line, sizeof(line), "WARN: ADC OVERRUN (%lu)\r\n", (unsigned long)local_res.overrun_cnt);
            uart_send_string(uart0, line);
            last_overrun = local_res.overrun_cnt;
        }
    }
}

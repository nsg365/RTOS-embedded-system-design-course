/*
 * tasks.h
 *
 *  Created on: 24-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#ifndef TASKS_H_
#define TASKS_H_

#include <stdint.h>
#include "os.h"
#include "dsp.h"

#define CMD_QUEUE_CAPACITY 8U

typedef enum {
    METRIC_NONE = 0,
    METRIC_RMS,
    METRIC_DOM_FREQ,
    METRIC_HARMONICS,
    CMD_TOGGLE_CONT
} cmd_type_t;

typedef struct {
    cmd_type_t      metric;
    float           value;       // RMS (V) or Dominant Frequency (Hz)
    dsp_harmonics_t harmonics;   // Fundamental + harmonic frequencies (Hz)
    uint32_t        overrun_cnt; // Snapshot of adc0->overrun
} shared_results_t;

// Shared IPC objects (defined in main.c)
extern mq_t             cmd_queue;
extern sem_t            results_sem;
extern shared_results_t g_results;

// RTOS tasks
void uart_rx_task(void *arg);
void dsp_task(void *arg);
void uart_tx_task(void *arg);

#endif /* TASKS_H_ */

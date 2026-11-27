/*
 * uart_rx_task.c
 *
 *  Created on: 24-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#include "tasks.h"
#include "uart.h"
#include <string.h>

#define CMD_BUF_LEN 16U

static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

void uart_rx_task(void *arg) {
    (void)arg;

    char cmd_buf[CMD_BUF_LEN];
    uint8_t idx = 0;
    uint8_t overflow = 0;

    while (1) {
        char c = uart_receive(uart0);

        if (c == '\r' || c == '\n') {
            if (overflow) {
                uart_send_string(uart0, "ERR: CMD TOO LONG\r\n");
            } else if (idx > 0) {
                cmd_buf[idx] = '\0';
                cmd_type_t cmd = METRIC_NONE;

                if (strcmp(cmd_buf, "RMS") == 0) {
                    cmd = METRIC_RMS;
                } else if (strcmp(cmd_buf, "FREQ") == 0) {
                    cmd = METRIC_DOM_FREQ;
                } else if (strcmp(cmd_buf, "HARM") == 0) {
                    cmd = METRIC_HARMONICS;
                } else if (strcmp(cmd_buf, "CONT") == 0) {
                    cmd = CMD_TOGGLE_CONT;
                }

                if (cmd != METRIC_NONE) {
                    if (mq_send(&cmd_queue, &cmd, 10) != OS_OK) {
                        uart_send_string(uart0, "ERR: CMD QUEUE FULL\r\n");
                    }
                } else {
                    uart_send_string(uart0, "ERR: UNKNOWN CMD\r\n");
                }
            }

            idx = 0;
            overflow = 0;
        } else if (!overflow) {
            if (idx < (CMD_BUF_LEN - 1U)) {
                cmd_buf[idx++] = to_upper(c);
            } else {
                overflow = 1;
            }
        }
    }
}

/*
 * uart.h
 *
 *  Created on: 05-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#include "os.h"

#ifndef UART_H_
#define UART_H_

#define BUF_SIZE 128

typedef struct {

    volatile uint32_t * const data_reg;
    volatile uint32_t * const im_reg;
    volatile uint32_t * const mis_reg;
    volatile uint32_t * const icr_reg;

    char tx_buf[BUF_SIZE];
    char rx_buf[BUF_SIZE];

    uint8_t tx_head;
    uint8_t tx_tail;
    uint8_t rx_head;
    uint8_t rx_tail;

    sem_t tx_sem;
    sem_t rx_sem;

    mutex_t uart_mutex;

} uart_t;

void uart0_init();

void uart1_init();

os_status_t uart_transmit(uart_t *uart, char data);

char uart_receive(uart_t *uart);

os_status_t uart_send_string(uart_t *uart, const char *data);

#endif /* UART_H_ */

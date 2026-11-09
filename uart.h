/*
 * uart.h
 *
 *  Created on: 05-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#include "os.h"

#ifndef UART_H_
#define UART_H_

void uart_init();

os_status_t uart_transmit(char data);

char uart_receive();

#endif /* UART_H_ */

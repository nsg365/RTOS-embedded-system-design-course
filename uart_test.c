/*
 * uart_test.c
 *
 *  Created on: 11-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#include "os.h"
#include "uart.h"
#include "tmrc123gh6pm.h"
#include <stdint.h>

static uint32_t stack_0[256];
static uint32_t stack_1[256];

void u0_task(void) {

    while(1) {

        uart_send_string(uart0,"UART0, not dead yet :)\r\n");

    }

}

void u1_task(void) {

    while(1) {

        uart_send_string(uart1,"UART1, because why not?\r\n");

    }

}

int main(void) {

    uart0_init();
    uart1_init();

    os_init();

    os_task_create(u0_task, NULL, "uart0", 1, stack_0, 256);
    os_task_create(u1_task, NULL, "uart1", 1, stack_1, 256);

    os_start();

    return 0;

}



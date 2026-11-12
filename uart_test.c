/*
 * uart_test.c
 *
 *  Created on: 11-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

// Built only when UART_TEST is defined (Project Properties > Build > Predefined Symbols);
// main.c's main() is left out then, so there is only one main().
#ifdef UART_TEST

#include "os.h"
#include "uart.h"
#include "tm4c123gh6pm.h"
#include <stdint.h>
#include <stddef.h>

static uint32_t stack_0[256];
static uint32_t stack_1[256];

void u0_task(void *arg) {

    while(1) {

        uart_send_string(uart0,"UART0, not dead yet :)\r\n");

    }

}

void u1_task(void *arg) {

    while(1) {

        uart_send_string(uart1,"UART1, because why not?\r\n");

    }

}

int main(void) {

    os_init();

    uart0_init();
    uart1_init();

    os_task_create(u0_task, NULL, "uart0", 1, stack_0, 256);
    os_task_create(u1_task, NULL, "uart1", 1, stack_1, 256);

    os_start();

    return 0;

}

#endif /* UART_TEST */

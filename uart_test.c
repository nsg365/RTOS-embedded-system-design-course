/*
 * uart_test.c
 *
 *  Created on: 11-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

// Built only when UART_TEST is defined (Project Properties > Build > Predefined Symbols);
// main.c's main() is left out then, so there is only one main().
#ifdef UART_TEST

#include <stddef.h>
#include "tm4c123gh6pm.h"
#include "os.h"
#include "dsp.h"
#include "tasks.h"
#include "uart.h"

// uart.h defines BUF_SIZE as 128 and adc.h defines BUF_SIZE as 256;
// undefine here to avoid macro redefinition warnings in main.c.
#undef BUF_SIZE
#include "adc.h"

#define RX_STACK_WORDS  192U
#define DSP_STACK_WORDS 256U
#define TX_STACK_WORDS  256U

// Shared IPC objects
mq_t             cmd_queue;
static uint8_t   cmd_queue_buf[CMD_QUEUE_CAPACITY * sizeof(cmd_type_t)];
sem_t            results_sem;
shared_results_t g_results;

// Task stacks (OS_MIN_STACK_WORDS is 64)
static uint32_t uart_rx_stack[RX_STACK_WORDS];
static uint32_t dsp_stack[DSP_STACK_WORDS];
static uint32_t uart_tx_stack[TX_STACK_WORDS];

static void system_init(void) {
    // 1. Enable FPU (CP10 and CP11 full access) before any float math runs
    NVIC_CPAC_R |= (0xFU << 20);
    __asm(" DSB");
    __asm(" ISB");

    // 2. Force 16 MHz PIOSC with PLL bypassed and no system divider
    //    Matches OS_CPU_HZ = 16 MHz and TIMER0_TAILR_R = 1600 - 1 (10 kHz)
    SYSCTL_RCC2_R &= ~0x80000000U;                       // Clear USERCC2 (use RCC)
    SYSCTL_RCC_R   = (SYSCTL_RCC_R & ~0x00400030U)       // Clear USESYSDIV (bit 22) & OSCSRC (bits 5:4)
                   | 0x00000810U;                        // Set BYPASS (bit 11) & OSCSRC = PIOSC (0x1)
}

int main(void) {
    system_init();

    os_init();
    dsp_init();
    uart0_init();
    adc0_init();

    // Initialize IPC primitives
    sem_init(&results_sem, 0, 1);
    mq_init(&cmd_queue, cmd_queue_buf, sizeof(cmd_type_t), CMD_QUEUE_CAPACITY);

    // Create tasks (0 = highest priority, 7 = OS_PRIO_IDLE)
    os_task_create(dsp_task,     NULL, "dsp",     1, dsp_stack,     DSP_STACK_WORDS);
    os_task_create(uart_tx_task, NULL, "uart_tx", 2, uart_tx_stack, TX_STACK_WORDS);
    os_task_create(uart_rx_task, NULL, "uart_rx", 3, uart_rx_stack, RX_STACK_WORDS);

    // Start ADC sampling and launch RTOS scheduler (never returns)
    adc0_start();
    os_start();

    while (1) {
    }
}

#endif /* UART_TEST */

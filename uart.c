/*
 * uart.c
 *
 *  Created on: 05-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#include "os.h"

#define BUF_SIZE 128

// Add functionality to select UART module and baud rate. Check whether we need to use FIFOs.

char uart0_tx_buf[BUF_SIZE];
char uart0_rx_buf[BUF_SIZE];

uint8_t tx0_buf_head, tx0_buf_tail;
uint8_t rx0_buf_head, rx0_buf_tail;

volatile sem_t uart0_tx_sem;
volatile sem_t uart0_rx_sem;

void uart_init() {

    tx0_buf_head = 0; tx0_buf_tail = 0;
    rx0_buf_head = 0; rx0_buf_tail = 0;

    sem_init(&uart0_tx_sem, BUF_SIZE, BUF_SIZE);
    sem_init(&uart0_rx_sem, 0, BUF_SIZE);

    SYSCTL_RCGCUART_R |= 0x01;
    SYSCTL_RCGCGPIO_R |= 0x01;
    GPIO_PORTA_AFSEL_R |= 0x03;
    GPIO_PORTA_PCTL_R |= 0x11;
    GPIO_PORTA_DEN_R |= 0x03;

    UART0_CTL_R &= 0xFFFFFFFE;

    //Baud Rate 115200
    UART0_IBRD_R = 0xA;
    UART0_FBRD_R = 0x2C;

    UART0_LCRH_R = 0x60;          // 8-bit, no parity, 1 stop
    UART0_CC_R = 0x00;

    UART0_IM_R |= (1<<4);
    NVIC_EN0_R |= (1<<5);

    UART0_CTL_R |= 0x301;

}

os_status_t uart_transmit(char data) {

    os_status_t status = sem_wait(&uart0_tx_sem, OS_FOREVER);

    if (status == OS_OK) {
        uart0_tx_buf[tx0_buf_head] = data;
        tx0_buf_head = (tx0_buf_head + 1) % BUF_SIZE;

        UART0_IM_R |= (1<<5);
    }

    return status;

}

char uart_receive() {

    os_status_t status = sem_wait(&uart0_rx_sem, OS_FOREVER);

    char data = 0;

    if (status == OS_OK) {
        data = uart0_rx_buf[rx0_buf_tail];
        rx0_buf_tail = (rx0_buf_tail + 1) % BUF_SIZE;
    }

    return data;

}

void UART0_Handler(void) {

    if (UART0_MIS_R & (1<<5)) {

        if (tx0_buf_tail != tx0_buf_head) {

            UART0_DR_R = uart0_tx_buf[tx0_buf_tail];
            tx0_buf_tail = (tx0_buf_tail + 1) % BUF_SIZE;
            sem_post_isr(&uart0_tx_sem);

        } else {

            UART0_IM_R &= ~(1<<5);

        }

        UART0_ICR_R = (1<<5);

    }

    if (UART0_MIS_R & (1<<4)) {

        if (rx0_buf_tail != (rx0_buf_head + 1) % BUF_SIZE) {

            uart0_rx_buf[rx0_buf_head] = UART0_DR_R;
            rx0_buf_head = (rx0_buf_head + 1) % BUF_SIZE;
            sem_post_isr(&uart0_rx_sem);

        } else {
            char dropped = UART0_DR_R;
        }

        UART0_ICR_R = (1<<4);

    }

}






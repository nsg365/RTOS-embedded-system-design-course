/*
 * uart.c
 *
 *  Created on: 05-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#include "tm4c123gh6pm.h"
#include "os.h"
#include "uart.h"
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

static uart_t uart0_instance = {

    .data_reg = &UART0_DR_R,
    .im_reg = &UART0_IM_R,
    .mis_reg = &UART0_MIS_R,
    .icr_reg = &UART0_ICR_R,

    .tx_head = 0,
    .tx_tail = 0,
    .rx_head = 0,
    .rx_tail = 0,

    .res_id = RES_UART0_TX

};

static uart_t uart1_instance = {

    .data_reg = &UART1_DR_R,
    .im_reg = &UART1_IM_R,
    .mis_reg = &UART1_MIS_R,
    .icr_reg = &UART1_ICR_R,

    .tx_head = 0,
    .tx_tail = 0,
    .rx_head = 0,
    .rx_tail = 0,

    .res_id = RES_UART1_TX

};

uart_t * const uart0 = &uart0_instance;
uart_t * const uart1 = &uart1_instance;

void uart0_init() {

    sem_init(&(uart0->tx_sem), BUF_SIZE, BUF_SIZE);
    sem_init(&(uart0->rx_sem), 0, BUF_SIZE);

    SYSCTL_RCGCUART_R |= 0x01;

    SYSCTL_RCGCGPIO_R |= 0x01;
    while ((SYSCTL_PRGPIO_R & 0x01) == 0);

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

void uart1_init() {

    sem_init(&(uart1->tx_sem), BUF_SIZE, BUF_SIZE);
    sem_init(&(uart1->rx_sem), 0, BUF_SIZE);

    SYSCTL_RCGCUART_R |= 0x02;

    SYSCTL_RCGCGPIO_R |= 0x02;
    while ((SYSCTL_PRGPIO_R & 0x02) == 0);

    GPIO_PORTB_AFSEL_R |= 0x03;
    GPIO_PORTB_PCTL_R |= 0x11;
    GPIO_PORTB_DEN_R |= 0x03;

    UART1_CTL_R &= 0xFFFFFFFE;

    //Baud Rate 115200
    UART1_IBRD_R = 0xA;
    UART1_FBRD_R = 0x2C;

    UART1_LCRH_R = 0x60;          // 8-bit, no parity, 1 stop
    UART1_CC_R = 0x00;

    UART1_IM_R |= (1<<4);
    NVIC_EN0_R |= (1<<6);

    UART1_CTL_R |= 0x301;

}

static os_status_t uart_transmit_raw(uart_t *uart, char data) {

    if (uart == NULL) {
        return OS_ERROR;
    }

    os_status_t status = sem_wait(&(uart->tx_sem), OS_FOREVER);

    if (status == OS_OK) {
        uart->tx_buf[uart->tx_head] = data;
        uart->tx_head = (uart->tx_head + 1) % BUF_SIZE;

        *(uart->im_reg) |= (1<<5);
    }

    return status;

}

os_status_t uart_transmit(uart_t *uart, char data) {

    if (uart == NULL) {
        return OS_ERROR;
    }

    os_status_t status;

    res_acquire(uart->res_id,OS_FOREVER);
    status = uart_transmit_raw(uart,data);
    res_release(uart->res_id);

    return status;

}

char uart_receive(uart_t *uart) {

    if (uart == NULL) {
        return 0;
    }

    os_status_t status = sem_wait(&(uart->rx_sem), OS_FOREVER);

    char data = 0;

    if (status == OS_OK) {
        res_acquire(uart->res_id,OS_FOREVER);
        data = uart->rx_buf[uart->rx_tail];
        uart->rx_tail = (uart->rx_tail + 1) % BUF_SIZE;
        res_release(uart->res_id);
    }

    return data;

}

os_status_t uart_send_string(uart_t *uart, const char *data) {

    if (uart == NULL || data == NULL) {
        return OS_ERROR;
    }

    os_status_t status;

    res_acquire(uart->res_id,OS_FOREVER);
    while (*data != '\0') {

        status = uart_transmit_raw(uart,*data);
        if(status == OS_OK) {
            data++;
            continue;
        } else {
            res_release(uart->res_id);
            return status;
        }

    }
    res_release(uart->res_id);

    return OS_OK;

}

void UART_Handler(uart_t *uart) {

    uint32_t int_status = *(uart->mis_reg);

    if (int_status & (1<<5)) {

        if (uart->tx_tail != uart->tx_head) {

            *(uart->data_reg) = uart->tx_buf[uart->tx_tail];
            uart->tx_tail = (uart->tx_tail + 1) % BUF_SIZE;
            sem_post_isr(&(uart->tx_sem));

        } else {

            *(uart->im_reg) &= ~(1<<5);

        }

        *(uart->icr_reg) = (1<<5);

    }

    if (int_status & (1<<4)) {

        if (uart->rx_tail != (uart->rx_head + 1) % BUF_SIZE) {

            uart->rx_buf[uart->rx_head] = *(uart->data_reg);
            uart->rx_head = (uart->rx_head + 1) % BUF_SIZE;
            sem_post_isr(&(uart->rx_sem));

        } else {
            char dropped = *(uart->data_reg);
        }

        *(uart->icr_reg) = (1<<4);

    }

}

void UART0_Handler() {
    UART_Handler(uart0);
}

void UART1_Handler() {
    UART_Handler(uart1);
}





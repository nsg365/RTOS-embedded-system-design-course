/*
 * uart.c
 *
 *  Created on: 05-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#include "tm4c123gh6pm.h"
#include "os.h"

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

} uart_t;

uart_t uart0 {

    .dr_reg = UART0_DR_R;
    .im_reg = UART0_IM_R;
    .mis_reg = UART0_MIS_R;
    .icr_reg = UART0_ICR_R;

    .tx_head = 0;
    .tx_tail = 0;
    .rx_head = 0;
    .rx_tail = 0;

}

uart_t uart1 {

    .dr_reg = UART1_DR_R;
    .im_reg = UART1_IM_R;
    .mis_reg = UART1_MIS_R;
    .icr_reg = UART1_ICR_R;

    .tx_head = 0;
    .tx_tail = 0;
    .rx_head = 0;
    .rx_tail = 0;

}

// Add functionality to select UART module and baud rate. Check whether we need to use FIFOs.

char uart0_tx_buf[BUF_SIZE];
char uart0_rx_buf[BUF_SIZE];

uint8_t tx0_buf_head, tx0_buf_tail;
uint8_t rx0_buf_head, rx0_buf_tail;

volatile sem_t uart0_tx_sem;
volatile sem_t uart0_rx_sem;

void uart_init(uart_t * uart) {

    sem_init(&(uart->tx_sem), BUF_SIZE, BUF_SIZE);
    sem_init(&(uart->rx_sem), 0, BUF_SIZE);

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

os_status_t uart_transmit(uart_t *uart, char data) {

    os_status_t status = sem_wait(&(uart->tx_sem), OS_FOREVER);

    if (status == OS_OK) {
        uart->tx_buf[uart->tx_head] = data;
        uart->tx_head = (uart->tx_head + 1) % BUF_SIZE;

        uart->im_reg |= (1<<5);
    }

    return status;

}

char uart_receive(uart_t *uart) {

    os_status_t status = sem_wait(&(uart->rx_sem), OS_FOREVER);

    char data = 0;

    if (status == OS_OK) {
        data = uart->rx_buf[uart->rx_tail];
        uart->rx_tail = (uart->rx_tail + 1) % BUF_SIZE;
    }

    return data;

}

void UART_Handler(uart_t *uart) {

    if (uart->mis_reg & (1<<5)) {

        if (uart->tx_tail != uart->tx_head) {

            uart->data_reg = uart->tx_buf[uart->tx_tail];
            uart->tx_tail = (uart->tx_tail + 1) % BUF_SIZE;
            sem_post_isr(&(uart->tx_sem));

        } else {

            uart->im_reg &= ~(1<<5);

        }

        uart->icr_reg = (1<<5);

    }

    if (uart->mis_reg & (1<<4)) {

        if (uart->rx_tail != (uart->rx_head + 1) % BUF_SIZE) {

            uart->rx_buf[uart->rx_head] = uart->data_reg;
            uart->rx_head = (uart->rx_head + 1) % BUF_SIZE;
            sem_post_isr(&(uart->rx_sem));

        } else {
            char dropped = uart->data_reg;
        }

        uart->icr_reg = (1<<4);

    }

}

void UART0_Handler() {
    UART_Handler(&uart0);
}

void UART1_Handler() {
    UART_Handler(&uart1);
}





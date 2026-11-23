/*
 * adc.c
 *
 *  Created on: 13-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#include "tm4c123gh6pm.h"
#include "os.h"
#include "adc.h"
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

// If needed, make a new function to pass custom sampling rates
// Uses Timer0 A, it performs the initialization as well. If using this, do NOT mess with Timer0 A before or after initializing ADC!

static adc_t adc0_instance = {

    .activeBuf = adc0_instance.buf0,
    .readyBuf = NULL,

    .head = 0,
    .ready = 0,
    .overrun = 0

};

adc_t * const adc0 = &adc0_instance;

static void timera_init(void) {

    SYSCTL_RCGCTIMER_R |= 0x1;
    while ((SYSCTL_PRTIMER_R & 0x1) == 0);

    TIMER0_CTL_R &= ~(0x1);

    TIMER0_CFG_R = 0x0;
    TIMER0_TAMR_R |= 0x2;
    TIMER0_TAILR_R = 1600 - 1;

    TIMER0_CTL_R |= 0x20; //adc trigger

}

void adc0_init(void) {

    sem_init(&(adc0->ready_sem),0,1);

    SYSCTL_RCGCADC_R |= 0x1;
    while((SYSCTL_PRADC_R & 0x1) == 0);

    SYSCTL_RCGCGPIO_R |= 0x10;
    while((SYSCTL_PRGPIO_R & 0x10) == 0);

    GPIO_PORTE_AFSEL_R |= 0x8;
    GPIO_PORTE_DEN_R &= ~(0x8);
    GPIO_PORTE_AMSEL_R |= 0x8;

    ADC0_ACTSS_R &= ~(0xF);

    ADC0_EMUX_R &= ~(0xF);
    ADC0_EMUX_R |= 0x5;

    ADC0_SSMUX0_R = 0x0;

    ADC0_SSCTL0_R |= 0x6;
    ADC0_IM_R |= 0x1;

    ADC0_ACTSS_R |= 0x1;

    NVIC_EN0_R = (1<<14);

    timera_init();

}

void adc0_start(void) {

    TIMER0_CTL_R |= 0x1;  //enabling

}

uint16_t * adc0_get() {

    uint16_t * buf = NULL;

    while (buf == NULL) {

        os_status_t status = sem_wait(&(adc0->ready_sem), OS_FOREVER);

        if (status != OS_OK) {
            return NULL;
        }

        NVIC_DIS0_R = (1<<14);

        if (adc0->ready) {

            adc0->ready = 0;
            buf = adc0->readyBuf;

        }

        NVIC_EN0_R = (1<<14);

    }

    return buf;

}

void ADCSeq0_Handler(void) {

    ADC0_ISC_R = 0x1;

    adc0->activeBuf[(adc0->head)++] = (uint16_t)(ADC0_SSFIFO0_R & 0xFFF);

    if (adc0->head == BUF_SIZE) {

        if (adc0->ready) {

            (adc0->overrun)++;

        }

        adc0->readyBuf = adc0->activeBuf;
        adc0->activeBuf = (adc0->readyBuf == adc0->buf0) ? adc0->buf1 : adc0->buf0;

        adc0->head = 0;
        adc0->ready = 1;

        sem_post_isr(&(adc0->ready_sem));

    }

}



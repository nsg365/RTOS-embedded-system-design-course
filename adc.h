/*
 * adc.h
 *
 *  Created on: 17-Nov-2026
 *      Author: Emil Ben, EE23BT024
 */

#ifndef ADC_H_
#define ADC_H_

#define BUF_SIZE 256

typedef struct {

    uint16_t buf0[BUF_SIZE];
    uint16_t buf1[BUF_SIZE];

    uint16_t * volatile activeBuf;
    uint16_t * volatile readyBuf;

    volatile uint32_t head;
    volatile uint32_t ready;
    volatile uint32_t overrun;

    sem_t ready_sem;

} adc_t;

extern adc_t * const adc0;

void adc0_init(void);
uint16_t * adc0_get();

#endif /* ADC_H_ */

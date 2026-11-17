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

    volatile uint16_t * activeBuf;
    volatile uint16_t * readyBuf;

    volatile uint32_t head;

    sem_t ready_sem;

} adc_t

extern adc_t * const adc0;

void adc0_init(void);
uint16_t * adc0_get();

#endif /* ADC_H_ */

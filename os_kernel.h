/*
 * os_kernel.h - Kernel-internal definitions. Not for application code.
 */
#ifndef OS_KERNEL_H
#define OS_KERNEL_H

#include "os.h"
#include "tm4c123gh6pm.h"

#define OS_STACK_CANARY     0xDEADBEEFu // fills every task stack; untouched words = unused stack

// Task Control Block. 'sp' MUST stay the first member: os_asm.asm reads it at offset 0.
typedef struct os_tcb {
    uint32_t      *sp;          // saved process stack pointer                 
    uint8_t        base_prio;   // priority given at creation                  
    uint8_t        prio;        // effective priority (priority inheritance)   
    task_state_t   state;
    uint32_t       wake_tick;   // os_sleep() / timeouts (phase 2)             
    void          *blocked_on;  // semaphore or mutex being waited on (phase 3)
    struct os_tcb *next;        // link in a wait list (phase 3)               
    uint32_t       run_ticks;   // ticks spent running                         
    uint32_t      *stack_base;  // lowest stack word; holds OS_STACK_CANARY    
    uint32_t       stack_words;
    const char    *name;
} os_tcb_t;

// Shared with os_asm.asm
extern os_tcb_t * volatile os_curr;     // pointer to the tcb of the currently running task

// Implemented in os_asm.asm 
void     PendSV_Handler(void);
void     os_hardfault_handler(void);
uint32_t os_crit_enter(void);           // disable interrupts, return old PRIMASK
void     os_crit_exit(uint32_t primask); // restore PRIMASK from os_crit_enter() 

// Implemented in os_core.c 
void     SysTick_Handler(void);
void     os_fault_report(uint32_t *frame); // called by os_hardfault_handler 

// Implemented in os_core.c, called only from PendSV_Handler
os_tcb_t *os_sched_select(void);

// Request a context switch; it happens when no other ISR is active.
#define OS_PEND_SWITCH() \
    do { NVIC_INT_CTRL_R = NVIC_INT_CTRL_PEND_SV; \
        __asm(" DSB"); \
        __asm(" ISB"); \
    } while (0)

#endif /* OS_KERNEL_H */

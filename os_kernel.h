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
    uint32_t       wake_tick;   // os_sleep() wake-up, or when a timed wait gives up
    uint8_t        timed_wait;  // 1 = BLOCKED with a timeout (wake_tick is valid)
    uint8_t        mutex_count; // mutexes currently held
    struct os_tcb **wait_list;  // wait list this task is BLOCKED in (NULL if none)
    mutex_t       *wait_mutex;  // mutex it is BLOCKED on, for priority inheritance
    os_status_t    wait_result; // OS_OK or OS_TIMEOUT, read when the wait ends
    struct os_tcb *next;        // link in a ready list or a wait list (never both)
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

// Blocking, used by os_sync.c. Call only with interrupts disabled.
void     os_block(os_tcb_t **list, uint32_t timeout_ms); // block os_curr; switch happens at os_crit_exit()
void     os_unblock(os_tcb_t *t, os_status_t result);    // take t off its wait list and make it READY
void     os_set_prio(os_tcb_t *t, uint8_t prio);         // change effective priority (priority inheritance)

// Request a context switch; it happens when no other ISR is active.
#define OS_PEND_SWITCH() \
    do { NVIC_INT_CTRL_R = NVIC_INT_CTRL_PEND_SV; \
        __asm(" DSB"); \
        __asm(" ISB"); \
    } while (0)

#endif /* OS_KERNEL_H */

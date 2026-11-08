// os_core.c - Task creation, scheduler, SysTick tick and fault reporting.
#include <stdint.h>
#include <stddef.h>
#include "os_kernel.h"

// State 
static os_tcb_t          tcbs[OS_MAX_TASKS];
static uint32_t          num_tasks;
static os_tcb_t         *idle_tcb;
static volatile uint32_t tick_count;
static uint32_t          slice_left;

os_tcb_t * volatile os_curr;            // read/written by PendSV_Handler

// Debug counters: watch these in the CCS Expressions view
volatile uint32_t os_switch_count;
volatile uint32_t os_idle_ticks;
volatile uint32_t os_fault_pc;
volatile uint32_t os_fault_lr;

static uint32_t idle_stack[OS_MIN_STACK_WORDS];

// Helper tasks
static void idle_task(void *arg)
{
    (void)arg;
    for (;;) {
        __asm(" WFI");                  // sleep until the next interrupt
    }
}

// A task function that returns lands here (its initial LR points here).
static void task_exit(void)
{
    os_crit_enter();
    os_curr->state = TASK_DEAD;
    os_crit_exit(0);
    for (;;) {
        os_yield();
    }
}

// API
void os_init(void)
{
    num_tasks  = 0;
    tick_count = 0;
    os_curr    = NULL;
    os_task_create(idle_task, NULL, "idle", OS_PRIO_IDLE, idle_stack, OS_MIN_STACK_WORDS);
    idle_tcb = &tcbs[0];
}

int32_t os_task_create(os_task_fn fn, void *arg, const char *name, uint8_t prio, uint32_t *stack, uint32_t stack_words)
{
    uint32_t  primask, i;
    uint32_t *sp;
    os_tcb_t *t;

    if (fn == NULL || stack == NULL || stack_words < OS_MIN_STACK_WORDS || prio >= OS_NUM_PRIO)
        return OS_ERROR;
    if (prio == OS_PRIO_IDLE && num_tasks != 0)
        return OS_ERROR;                // lowest priority is reserved for idle

    primask = os_crit_enter();
    if (num_tasks >= OS_MAX_TASKS) {
        os_crit_exit(primask);
        return OS_ERROR;
    }
    t = &tcbs[num_tasks];

    // Fill the stack so the unused part can be measured later
    for (i = 0; i < stack_words; i++)
        stack[i] = OS_STACK_CANARY;

    // Top of stack, 8-byte aligned as required for exception frames
    sp = (uint32_t *)((uint32_t)(stack + stack_words) & ~7u);

    // Hardware exception frame: popped automatically on exception return
    *--sp = 0x01000000u;                // xPSR: Thumb bit set
    *--sp = (uint32_t)fn & ~1u;         // PC: task entry (bit 0 clear)
    *--sp = (uint32_t)task_exit;        // LR: where a returning task goes
    sp -= 4;                            // R12, R3, R2, R1: don't care (canary)
    *--sp = (uint32_t)arg;              // R0: task argument

    /* Software frame: popped by PendSV_Handler (LDMIA {R4-R11, LR}) */
    *--sp = 0xFFFFFFFDu;                // EXC_RETURN: Thread mode, PSP, no FPU frame
    sp -= 8;                            // R11..R4: don't care (canary)

    t->sp          = sp;
    t->base_prio   = prio;
    t->prio        = prio;
    t->state       = TASK_READY;
    t->wake_tick   = 0;
    t->blocked_on  = NULL;
    t->next        = NULL;
    t->run_ticks   = 0;
    t->stack_base  = stack;
    t->stack_words = stack_words;
    t->name        = name;
    num_tasks++;

    os_crit_exit(primask);
    return (int32_t)(t - tcbs);
}

void os_start(void)
{
    os_crit_enter();

    // PendSV lowest (7) so it only runs after every other ISR; SysTick just above (6)
    NVIC_SYS_PRI3_R = (NVIC_SYS_PRI3_R & 0x00FFFFFFu) | (6u << 29);
    NVIC_SYS_PRI3_R = (NVIC_SYS_PRI3_R & 0xFF00FFFFu) | (7u << 21);

    // SysTick: 1 ms tick from the system clock
    NVIC_ST_CTRL_R    = 0;
    NVIC_ST_RELOAD_R  = OS_CPU_HZ / OS_TICK_HZ - 1u;
    NVIC_ST_CURRENT_R = 0;
    NVIC_ST_CTRL_R    = NVIC_ST_CTRL_CLK_SRC | NVIC_ST_CTRL_INTEN | NVIC_ST_CTRL_ENABLE;

    // os_curr == NULL tells PendSV there is no context to save. The first switch returns with EXC_RETURN 0xFFFFFFFD, so Thread mode moves to PSP.
    os_curr    = NULL;
    slice_left = OS_TIME_SLICE;
    OS_PEND_SWITCH();
    os_crit_exit(0);                    // PendSV runs here and never comes back

    for (;;) {
    }
}

void os_yield(void)
{
    OS_PEND_SWITCH();
}

uint32_t os_ticks(void)
{
    return tick_count;
}

// Scheduler

// Called only from PendSV_Handler with interrupts disabled. Returns the TCB to run next; PendSV stores it in os_curr.
os_tcb_t *os_sched_select(void)
{
    uint32_t  start, i;
    os_tcb_t *t, *pick = idle_tcb;

    if (os_curr != NULL && os_curr->state == TASK_RUNNING)
        os_curr->state = TASK_READY;

    /* Round robin: scan from the task after the current one, wrapping around
       (the current task is checked last, so it keeps running if it is alone). */
    start = (os_curr == NULL) ? 0u : (uint32_t)(os_curr - tcbs);
    for (i = 1; i <= num_tasks; i++) {
        t = &tcbs[(start + i) % num_tasks];
        if (t != idle_tcb && t->state == TASK_READY) {
            pick = t;
            break;
        }
    }

    pick->state = TASK_RUNNING;
    slice_left  = OS_TIME_SLICE;
    os_switch_count++;
    return pick;
}

void SysTick_Handler(void)
{
    tick_count++;
    if (os_curr == NULL)
        return;

    os_curr->run_ticks++;
    if (os_curr == idle_tcb)
        os_idle_ticks++;

    if (--slice_left == 0) {
        slice_left = OS_TIME_SLICE;
        OS_PEND_SWITCH();
    }
}

// Fault report

/* frame points at the stacked R0..xPSR of the code that faulted. */
void os_fault_report(uint32_t *frame)
{
    os_fault_lr = frame[5];
    os_fault_pc = frame[6];             /* look this address up in the .map file */

    /* All three LEDs on (white) = kernel fault */
    SYSCTL_RCGCGPIO_R |= 0x20;
    while ((SYSCTL_PRGPIO_R & 0x20) == 0) {
    }
    GPIO_PORTF_DIR_R |= 0x0E;
    GPIO_PORTF_DEN_R |= 0x0E;
    GPIO_PORTF_DATA_R |= 0x0E;

    for (;;) {
    }
}

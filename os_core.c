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

// Ready lists: one FIFO of READY tasks per priority. The running task is not in any list.
static os_tcb_t         *ready_head[OS_NUM_PRIO]; // first task in each priority's queue
static os_tcb_t         *ready_tail[OS_NUM_PRIO]; // last task in each priority's queue
static uint32_t          ready_bitmap;  // bit p set = ready_head[p] is not empty

os_tcb_t * volatile os_curr;            // read/written by PendSV_Handler

// Debug counters: watch these in the CCS Expressions view
volatile uint32_t os_switch_count;
volatile uint32_t os_idle_ticks;
volatile uint32_t os_fault_pc;
volatile uint32_t os_fault_lr;

static uint32_t idle_stack[OS_MIN_STACK_WORDS];

// Ready lists. Call only with interrupts disabled.

// Append t to the tail of its priority's list, so equal-priority tasks take turns.
static void ready_push(os_tcb_t *t)
{
    uint8_t p = t->prio;

    t->state = TASK_READY;
    t->next  = NULL;
    if (ready_head[p] == NULL)
        ready_head[p] = t;
    else
        ready_tail[p]->next = t;
    ready_tail[p] = t;
    ready_bitmap |= 1u << p;
}

// Remove and return the first task of priority p. The list must not be empty.
static os_tcb_t *ready_pop(uint8_t p)
{
    os_tcb_t *t = ready_head[p];

    ready_head[p] = t->next;
    if (ready_head[p] == NULL)
        ready_bitmap &= ~(1u << p);
    t->next = NULL;
    return t;
}

// Highest priority (lowest number) that has a READY task: the lowest set bit.
// The idle task is always ready or running, so some bit is set whenever this is called.
static uint8_t ready_highest(void)
{
    uint8_t p = 0;

    while ((ready_bitmap & (1u << p)) == 0)
        p++;
    return p;
}

// Take t out of the middle of its ready list (used when its priority changes).
static void ready_remove(os_tcb_t *t)
{
    uint8_t   p    = t->prio;
    os_tcb_t *prev = NULL;
    os_tcb_t *cur  = ready_head[p];

    while (cur != NULL && cur != t) {
        prev = cur;
        cur  = cur->next;
    }
    if (cur == NULL)
        return;
    if (prev == NULL)
        ready_head[p] = t->next;
    else
        prev->next = t->next;
    if (ready_tail[p] == t)
        ready_tail[p] = prev;
    if (ready_head[p] == NULL)
        ready_bitmap &= ~(1u << p);
    t->next = NULL;
}

// Wait lists (semaphore / mutex waiters): sorted by priority, highest first,
// FIFO among equal priorities. 'link' walks the list by pointing at each 'next'
// field in turn, so inserting at the head needs no special case.
static void wait_insert(os_tcb_t **list, os_tcb_t *t)
{
    os_tcb_t **link = list;

    while (*link != NULL && (*link)->prio <= t->prio)
        link = &(*link)->next;
    t->next = *link;
    *link   = t;
}

static void wait_remove(os_tcb_t **list, os_tcb_t *t)
{
    os_tcb_t **link = list;

    while (*link != NULL && *link != t)
        link = &(*link)->next;
    if (*link == t)
        *link = t->next;
    t->next = NULL;
}

// Request a switch if some READY task now outranks the running one.
static void preempt_check(void)
{
    if (os_curr != NULL && (ready_bitmap & ((1u << os_curr->prio) - 1u)))
        OS_PEND_SWITCH();
}

// Blocking. Call only with interrupts disabled.

// Block the running task on 'list'. The switch happens when the caller calls os_crit_exit().
void os_block(os_tcb_t **list, uint32_t timeout_ms)
{
    if (timeout_ms > 0x7FFFFFFFu && timeout_ms != OS_FOREVER)
        timeout_ms = 0x7FFFFFFFu;       // the timeout test in SysTick_Handler is signed

    os_curr->state       = TASK_BLOCKED;
    os_curr->wait_list   = list;
    os_curr->wait_result = OS_OK;
    os_curr->timed_wait  = (timeout_ms != OS_FOREVER);
    os_curr->wake_tick   = tick_count + timeout_ms;
    wait_insert(list, os_curr);
    OS_PEND_SWITCH();
}

// Take t off its wait list and make it READY. result: OS_OK (got it) or OS_TIMEOUT.
void os_unblock(os_tcb_t *t, os_status_t result)
{
    wait_remove(t->wait_list, t);
    t->wait_list   = NULL;
    t->wait_mutex  = NULL;
    t->timed_wait  = 0;
    t->wait_result = result;
    ready_push(t);
    preempt_check();
}

// Change t's effective priority, keeping whichever list it is in sorted.
void os_set_prio(os_tcb_t *t, uint8_t prio)
{
    if (t->prio == prio)
        return;
    if (t->state == TASK_READY) {
        ready_remove(t);
        t->prio = prio;
        ready_push(t);
    } else if (t->state == TASK_BLOCKED) {
        wait_remove(t->wait_list, t);
        t->prio = prio;
        wait_insert(t->wait_list, t);
    } else {
        t->prio = prio;                 // RUNNING or SLEEPING: not in any list
    }
    preempt_check();                    // e.g. the running task just dropped its priority
}

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
    os_curr->state = TASK_DEAD;         // not RUNNING, so the scheduler drops it
    os_crit_exit(0);
    for (;;) {
        os_yield();
    }
}

// API
void os_init(void)
{
    uint32_t p;

    num_tasks    = 0;
    tick_count   = 0;
    os_curr      = NULL;
    ready_bitmap = 0;
    for (p = 0; p < OS_NUM_PRIO; p++) {
        ready_head[p] = NULL;
        ready_tail[p] = NULL;
    }
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

    /* Software frame: loaded by PendSV_Handler (R4-R11 + EXC_RETURN) */
    *--sp = 0xFFFFFFFDu;                // EXC_RETURN: Thread mode, PSP, no FPU frame. (EXC_RETURN contains information about how the processor should return from the exception)
    sp -= 8;                            // R11..R4: don't care (canary)

    t->sp          = sp;
    t->base_prio   = prio;
    t->prio        = prio;
    t->wake_tick   = 0;
    t->timed_wait  = 0;
    t->mutex_count = 0;
    t->wait_list   = NULL;
    t->wait_mutex  = NULL;
    t->wait_result = OS_OK;
    t->run_ticks   = 0;
    t->stack_base  = stack;
    t->stack_words = stack_words;
    t->name        = name;
    num_tasks++;
    ready_push(t);                      // sets state = TASK_READY

    // Created by a running task with a higher priority than itself: run it now
    if (os_curr != NULL && prio < os_curr->prio)
        OS_PEND_SWITCH();

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

// Sleep for at least ms-1 and at most ms milliseconds (1 tick = 1 ms). Tasks only.
void os_sleep(uint32_t ms)
{
    uint32_t primask;

    if (ms == 0) {
        os_yield();
        return;
    }
    if (ms > 0x7FFFFFFFu)
        ms = 0x7FFFFFFFu;               // the wake-up test in SysTick_Handler is signed

    primask = os_crit_enter();
    os_curr->wake_tick = tick_count + ms;
    os_curr->state     = TASK_SLEEPING; // not RUNNING, so the scheduler leaves it out
    OS_PEND_SWITCH();
    os_crit_exit(primask);              // PendSV switches away here
}

// Scheduler

// Called only from PendSV_Handler with interrupts disabled. Returns the TCB to run next; PendSV stores it in os_curr.
os_tcb_t *os_sched_select(void)
{
    os_tcb_t *next;

    // Still RUNNING = preempted or yielded: back of its ready list.
    // SLEEPING or DEAD = it gave up the CPU, so it is left out.
    if (os_curr != NULL && os_curr->state == TASK_RUNNING)
        ready_push(os_curr);

    next = ready_pop(ready_highest());
    next->state = TASK_RUNNING;
    slice_left  = OS_TIME_SLICE;
    if (next != os_curr)
        os_switch_count++;
    return next;
}

void SysTick_Handler(void)
{
    uint32_t  primask, i;
    os_tcb_t *t;
    uint32_t  switch_needed = 0;

    tick_count++;
    if (os_curr == NULL)
        return;

    os_curr->run_ticks++;
    if (os_curr == idle_tcb)
        os_idle_ticks++;

    primask = os_crit_enter();          // higher-priority ISRs may touch the ready lists too (phase 3)

    // Wake every sleeping task whose time has come, and end every timed wait that has run out.
    // Signed difference so it still works when tick_count wraps around.
    for (i = 0; i < num_tasks; i++) {
        t = &tcbs[i];
        if ((int32_t)(tick_count - t->wake_tick) < 0)
            continue;
        if (t->state == TASK_SLEEPING) {
            ready_push(t);
            if (t->prio < os_curr->prio)
                switch_needed = 1;      // preempt the running task now
        } else if (t->state == TASK_BLOCKED && t->timed_wait) {
            os_unblock(t, OS_TIMEOUT);  // requests the switch itself if needed
        }
    }

    // Time slice over: switch only if a task of the same or higher priority is waiting (bits 0..prio)
    if (--slice_left == 0) {
        slice_left = OS_TIME_SLICE;
        if (ready_bitmap & ((2u << os_curr->prio) - 1u))
            switch_needed = 1;
    }

    if (switch_needed)
        OS_PEND_SWITCH();
    os_crit_exit(primask);
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

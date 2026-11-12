// os_sync.c - Semaphores, mutexes with priority inheritance, and the resource manager.
//
// Waiting tasks are BLOCKED on the object's wait list (highest priority first) and use
// no CPU. The blocking pattern used everywhere below:
//     primask = os_crit_enter();
//     ... can't have it yet ...
//     os_block(&obj->waiters, timeout);    // marks us BLOCKED, requests a switch
//     os_crit_exit(primask);               // the switch happens here; we resume when woken
//     return os_curr->wait_result;         // OS_OK (got it) or OS_TIMEOUT
// So these calls are for tasks only, and never with interrupts already disabled.
#include <stdint.h>
#include <stddef.h>
#include "os_kernel.h"

// Semaphores

void sem_init(sem_t *s, int32_t initial, int32_t max)
{
    s->count   = initial;
    s->max     = max;
    s->waiters = NULL;
}

// Take one count, waiting up to timeout_ms (0 = don't wait, OS_FOREVER = no limit).
os_status_t sem_wait(sem_t *s, uint32_t timeout_ms)
{
    uint32_t primask = os_crit_enter();

    if (s->count > 0) {
        s->count--;
        os_crit_exit(primask);
        return OS_OK;
    }
    if (timeout_ms == 0) {
        os_crit_exit(primask);
        return OS_TIMEOUT;
    }
    if (os_curr == NULL || primask != 0) {
        os_crit_exit(primask);
        return OS_ERROR;                // before os_start(), or interrupts were off: can't switch away
    }

    os_block(&s->waiters, timeout_ms);
    os_crit_exit(primask);              // switch away; back on sem_post or timeout
    return os_curr->wait_result;        // OS_OK: the post handed its count straight to us
}

// Give one count. If a task is waiting, the count goes straight to it (highest priority first).
static void sem_give(sem_t *s)
{
    uint32_t primask = os_crit_enter();

    if (s->waiters != NULL)
        os_unblock(s->waiters, OS_OK);
    else if (s->count < s->max)
        s->count++;                     // at max: the extra post is dropped
    os_crit_exit(primask);
}

void sem_post(sem_t *s)
{
    sem_give(s);
}

// Safe in an ISR: it never waits. If it wakes a higher-priority task, the switch
// happens (in PendSV) as soon as the ISR returns.
void sem_post_isr(sem_t *s)
{
    sem_give(s);
}

// Mutexes with priority inheritance

void mutex_init(mutex_t *m)
{
    m->owner   = NULL;
    m->waiters = NULL;
}

os_status_t mutex_lock(mutex_t *m, uint32_t timeout_ms)
{
    uint32_t  primask;
    os_tcb_t *owner;

    if (os_curr == NULL)
        return OS_ERROR;                // before os_start(): there is no task to own it

    primask = os_crit_enter();
    if (m->owner == NULL) {
        m->owner = os_curr;
        os_curr->mutex_count++;
        os_crit_exit(primask);
        return OS_OK;
    }
    if (m->owner == os_curr) {
        os_crit_exit(primask);
        return OS_ERROR;                // already ours: waiting would deadlock
    }
    if (timeout_ms == 0) {
        os_crit_exit(primask);
        return OS_TIMEOUT;
    }
    if (primask != 0) {
        os_crit_exit(primask);
        return OS_ERROR;                // interrupts were off: can't switch away
    }

    /* Priority inheritance: the owner runs at our priority until it unlocks, so a
       medium-priority task can't keep it (and therefore us) off the CPU. If the
       owner is itself waiting on another mutex, pass the boost along the chain. */
    owner = m->owner;
    while (owner != NULL && os_curr->prio < owner->prio) {
        os_set_prio(owner, os_curr->prio);
        if (owner->state == TASK_BLOCKED && owner->wait_mutex != NULL)
            owner = owner->wait_mutex->owner;
        else
            owner = NULL;
    }

    os_curr->wait_mutex = m;
    os_block(&m->waiters, timeout_ms);
    os_crit_exit(primask);              // switch away; back on unlock or timeout
    return os_curr->wait_result;        // OS_OK: mutex_unlock already made us the owner
}

os_status_t mutex_unlock(mutex_t *m)
{
    uint32_t  primask = os_crit_enter();
    os_tcb_t *next;

    if (m->owner != os_curr || os_curr == NULL) {
        os_crit_exit(primask);
        return OS_ERROR;                // only the owner may unlock
    }

    /* Give back any inherited priority once the task holds no mutex at all.
       (Holding several: it stays boosted until the last one is released.) */
    os_curr->mutex_count--;
    if (os_curr->mutex_count == 0)
        os_set_prio(os_curr, os_curr->base_prio);

    // Hand the mutex straight to the highest-priority waiter, so no other task can grab it first
    next = m->waiters;
    if (next != NULL) {
        m->owner = next;
        next->mutex_count++;
        os_unblock(next, OS_OK);
    } else {
        m->owner = NULL;
    }
    os_crit_exit(primask);              // a higher-priority new owner runs from here
    return OS_OK;
}

// Resource manager: one mutex per shared resource, plus usage statistics

typedef struct {
    mutex_t     lock;
    res_stats_t stats;
    uint32_t    taken_at;               // tick when last acquired, for max_hold_ticks
} resource_t;

// One entry per res_id_t, in the same order as the enum in os.h
static resource_t res_table[RES_COUNT] = {
    { { NULL, NULL }, { "uart0_tx", 0, 0, 0 }, 0 }, // RES_UART0_TX
    { { NULL, NULL }, { "uart1_tx", 0, 0, 0 }, 0 }, // RES_UART1_TX
    { { NULL, NULL }, { "results",  0, 0, 0 }, 0 }, // RES_RESULTS
};

os_status_t res_acquire(res_id_t id, uint32_t timeout_ms)
{
    resource_t *r;
    uint32_t    primask;
    os_status_t st;

    if ((uint32_t)id >= RES_COUNT)
        return OS_ERROR;
    r = &res_table[id];

    primask = os_crit_enter();
    if (r->lock.owner != NULL && r->lock.owner != os_curr)
        r->stats.contentions++;         // someone else has it: we will have to wait
    os_crit_exit(primask);

    st = mutex_lock(&r->lock, timeout_ms);
    if (st == OS_OK) {
        r->stats.acquisitions++;        // only the owner gets here, so no race
        r->taken_at = os_ticks();
    }
    return st;
}

os_status_t res_release(res_id_t id)
{
    resource_t *r;
    uint32_t    held;

    if ((uint32_t)id >= RES_COUNT)
        return OS_ERROR;
    r = &res_table[id];
    if (r->lock.owner != os_curr)
        return OS_ERROR;                // not ours: leave the stats alone

    held = os_ticks() - r->taken_at;
    if (held > r->stats.max_hold_ticks)
        r->stats.max_hold_ticks = held;
    return mutex_unlock(&r->lock);
}

os_status_t res_get_stats(res_id_t id, res_stats_t *out)
{
    uint32_t primask;

    if ((uint32_t)id >= RES_COUNT || out == NULL)
        return OS_ERROR;
    primask = os_crit_enter();          // copy all fields from the same moment
    *out = res_table[id].stats;
    os_crit_exit(primask);
    return OS_OK;
}

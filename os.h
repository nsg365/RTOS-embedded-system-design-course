/*
 * os.h - Public interface of the RTOS.
 * Implementations arrive in phases:
 *   P1: os_init, os_start, os_task_create, os_yield, os_ticks
 *   P2: os_sleep, priority scheduling
 *   P3: semaphores, mutexes, message queue, resource manager, statistics
 *
 * Rules for callers:
 *   ISRs may only call sem_post_isr() and os_ticks(). They must never wait.
 *   Priority 0 is the highest; OS_PRIO_IDLE is reserved for the idle task.
 */
#ifndef OS_H
#define OS_H

#include <stdint.h>

/* Config */
#define OS_CPU_HZ           16000000u   // system clock - 16 MHz
#define OS_TICK_HZ          1000u       // SysTick rate: 1 ms tick
 #define OS_TIME_SLICE       10u         // round-robin slice, in ticks
//#define OS_TIME_SLICE       500u //only for testing if task switching between red and blue is working properly. comment this and uncomment the above line to get the default time slice of 10ms
#define OS_MAX_TASKS        8u          // including the idle task
#define OS_NUM_PRIO         8u          // priorities 0 (highest) .. 7
#define OS_PRIO_IDLE        (OS_NUM_PRIO - 1u)
#define OS_MIN_STACK_WORDS  64u

// Return values
typedef int32_t os_status_t;
#define OS_OK               0
#define OS_TIMEOUT          (-1)
#define OS_ERROR            (-2)

// Timeout values
#define OS_FOREVER          0xFFFFFFFFu // timeout value: wait indefinitely

typedef void (*os_task_fn)(void *arg);

// Task management (P1)
void        os_init(void);
int32_t     os_task_create(os_task_fn fn, void *arg, const char *name,
                           uint8_t prio, uint32_t *stack, uint32_t stack_words);
void        os_start(void);             // never returns
void        os_yield(void);
uint32_t    os_ticks(void);             // ms since os_start()

// P2
void        os_sleep(uint32_t ms);

// Semaphores (P3)
struct os_tcb;

typedef struct {
    volatile int32_t count;
    int32_t          max;               // 1 for a binary semaphore 
    struct os_tcb   *waiters;           // blocked tasks, highest priority first
} sem_t;

void        sem_init(sem_t *s, int32_t initial, int32_t max);
os_status_t sem_wait(sem_t *s, uint32_t timeout_ms);
void        sem_post(sem_t *s);
void        sem_post_isr(sem_t *s);     // the only call allowed in an ISR

// Mutex with priority inheritance (P3)
typedef struct {
    struct os_tcb   *owner;
    struct os_tcb   *waiters;
} mutex_t;

void        mutex_init(mutex_t *m);
os_status_t mutex_lock(mutex_t *m, uint32_t timeout_ms);
os_status_t mutex_unlock(mutex_t *m);   // OS_ERROR if caller is not the owner

// Resource manager (P3)
typedef enum {
    RES_UART_TX,                        // UART transmit (terminal output)
    RES_RESULTS,                        // FFT results shared T1 -> T2
    RES_COUNT
} res_id_t;

typedef struct {
    const char *name;
    uint32_t    acquisitions;           // times granted
    uint32_t    contentions;            // times a task had to wait
    uint32_t    max_hold_ticks;         // longest time held
} res_stats_t;

os_status_t res_acquire(res_id_t id, uint32_t timeout_ms);
os_status_t res_release(res_id_t id);
os_status_t res_get_stats(res_id_t id, res_stats_t *out);

//  Message queue (P3) */
typedef struct {
    uint8_t    *buf;                    // capacity * msg_size bytes, caller-owned
    uint32_t    msg_size;
    uint32_t    capacity;
    uint32_t    head, tail;
    sem_t       slots;                  // free slots
    sem_t       items;                  // queued items
} mq_t;

void        mq_init(mq_t *q, void *buf, uint32_t msg_size, uint32_t capacity);
os_status_t mq_send(mq_t *q, const void *msg, uint32_t timeout_ms);
os_status_t mq_receive(mq_t *q, void *msg, uint32_t timeout_ms);

// Kernel statistics (P3)
typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SLEEPING, TASK_DEAD } task_state_t;

typedef struct {
    const char   *name;
    uint8_t       prio;
    task_state_t  state;
    uint8_t       cpu_percent;
    uint32_t      stack_free_words;
} os_task_info_t;

os_status_t os_get_task_info(uint32_t index, os_task_info_t *out);
uint32_t    os_task_count(void);

#endif /* OS_H */

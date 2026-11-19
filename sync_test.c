/*
 * sync_test.c - Phase 3 test: mutex priority inheritance, timeouts, semaphore wake-up.
 * Built only when SYNC_TEST is defined (Project Properties > Build > Predefined Symbols,
 * instead of UART_TEST); main.c's main() is left out then.
 *
 * Three tasks, H (priority 1), M (2) and L (3), follow a fixed timeline (in ms):
 *
 *   Priority inheritance
 *     0    L locks mtx and works (busy) until 100, then unlocks
 *     20   H wakes and asks for mtx -> blocks; L should now run at priority 1
 *     40   M wakes and wants to work (busy) for 200 ms
 *          With inheritance: M (2) can't preempt L (now 1), so L finishes at 100,
 *          H gets mtx at ~100, and only then does M run.
 *          Without it: M preempts L at 40 and H waits until ~240.
 *   Timeouts
 *     400  L locks mtx2 and sleeps 100 ms holding it
 *     420  H: mutex_lock(&mtx2, 30) -> OS_TIMEOUT after 30 ms
 *          H: mutex_unlock(&mtx2)   -> OS_ERROR (not the owner)
 *          H: sem_wait(&sem_never, 50) -> OS_TIMEOUT after 50 ms
 *     500  L unlocks mtx2 -> back to priority 3
 *   Semaphore wake-up
 *     600  H: sem_wait(&sem_go, OS_FOREVER) -> blocks
 *     650  L: sem_post(&sem_go) -> H (higher priority) must run before L's next line
 *
 * Result (about 0.7 s after reset):
 *   Blue while running, then green = all checks passed, red = something failed.
 *   Expressions view: sync_fail (0 = pass; bit n set = check n failed, see CHK_*),
 *   plus the raw times/priorities below.
 */
#ifdef SYNC_TEST

#include <stdint.h>
#include <stddef.h>
#include "tm4c123gh6pm.h"
#include "os.h"
#include "os_kernel.h"                  // test only: reads os_curr->prio to see the boost

#define LED_RED     0x02u               // PF1
#define LED_BLUE    0x04u               // PF2
#define LED_GREEN   0x08u               // PF3

#define PRIO_H      1u
#define PRIO_M      2u
#define PRIO_L      3u

// One bit per check in sync_fail
#define CHK_PI_LOCK_OK      (1u << 0)   // H got mtx (OS_OK)
#define CHK_PI_BOOST        (1u << 1)   // L ran at H's priority while H waited
#define CHK_PI_RESTORE      (1u << 2)   // L back to its own priority after unlocking
#define CHK_PI_ORDER        (1u << 3)   // H got mtx before M ever ran
#define CHK_PI_WAIT         (1u << 4)   // H waited only for L's remaining work (~80 ms)
#define CHK_MTX_TIMEOUT     (1u << 5)   // mutex_lock timed out after 30 ms
#define CHK_MTX_NOT_OWNER   (1u << 6)   // unlock by a non-owner refused
#define CHK_SEM_TIMEOUT     (1u << 7)   // sem_wait timed out after 50 ms
#define CHK_RESTORE_AFTER_TO (1u << 8)  // L back to priority 3 after a timed-out waiter
#define CHK_SEM_WAKE        (1u << 9)   // sem_post woke H immediately

static mutex_t mtx, mtx2;
static sem_t   sem_never, sem_go;

// Results, for the Expressions view
volatile uint32_t sync_fail = 0xFFFFFFFFu;  // stays all-ones until H finishes
volatile uint32_t h_req, h_got, m_first_run;
volatile uint32_t l_min_prio = 0xFFu, l_prio_after, l_prio_after2;
volatile uint32_t mtx_to_ms, sem_to_ms;
volatile uint32_t post_done;

static uint32_t stack_h[256];
static uint32_t stack_m[256];
static uint32_t stack_l[256];

// Sleep until os_ticks() reaches t (returns at once if t has already passed)
static void sleep_until(uint32_t t)
{
    uint32_t now = os_ticks();
    if ((int32_t)(t - now) > 0)
        os_sleep(t - now);
}

// Keep the CPU busy until os_ticks() reaches t (this task only advances while it runs)
static void busy_until(uint32_t t)
{
    while ((int32_t)(os_ticks() - t) < 0) {
    }
}

static void h_task(void *arg)
{
    uint32_t    fail = 0;
    uint32_t    t0;
    os_status_t st;

    (void)arg;

    // Priority inheritance
    sleep_until(20);
    h_req = os_ticks();
    st = mutex_lock(&mtx, 500);
    h_got = os_ticks();
    if (st != OS_OK)
        fail |= CHK_PI_LOCK_OK;
    else
        mutex_unlock(&mtx);

    // Timeouts
    sleep_until(420);
    t0 = os_ticks();
    st = mutex_lock(&mtx2, 30);         // L holds mtx2 until 500
    mtx_to_ms = os_ticks() - t0;
    if (st != OS_TIMEOUT || mtx_to_ms < 30 || mtx_to_ms > 32)
        fail |= CHK_MTX_TIMEOUT;

    if (mutex_unlock(&mtx2) != OS_ERROR)
        fail |= CHK_MTX_NOT_OWNER;

    t0 = os_ticks();
    st = sem_wait(&sem_never, 50);      // nobody ever posts this one
    sem_to_ms = os_ticks() - t0;
    if (st != OS_TIMEOUT || sem_to_ms < 50 || sem_to_ms > 52)
        fail |= CHK_SEM_TIMEOUT;

    // Semaphore wake-up: L posts at 650
    sleep_until(600);
    st = sem_wait(&sem_go, OS_FOREVER);
    if (st != OS_OK || post_done != 0)  // post_done = 1 means L ran on after posting
        fail |= CHK_SEM_WAKE;

    // Checks on what L and M recorded
    sleep_until(700);
    if (l_min_prio != PRIO_H)
        fail |= CHK_PI_BOOST;
    if (l_prio_after != PRIO_L)
        fail |= CHK_PI_RESTORE;
    if (m_first_run < h_got)            // same tick is fine: H got it, unlocked and slept within 1 ms
        fail |= CHK_PI_ORDER;
    if (h_got - h_req > 85)
        fail |= CHK_PI_WAIT;
    if (l_prio_after2 != PRIO_L)
        fail |= CHK_RESTORE_AFTER_TO;

    sync_fail = fail;
    GPIO_PORTF_DATA_R = (fail == 0) ? LED_GREEN : LED_RED;

    while (1)
        os_sleep(1000);
}

static void m_task(void *arg)
{
    (void)arg;

    sleep_until(40);
    m_first_run = os_ticks();           // with inheritance, not before L unlocks at ~100
    busy_until(m_first_run + 200);

    while (1)
        os_sleep(1000);
}

static void l_task(void *arg)
{
    (void)arg;

    // Priority inheritance: hold mtx while working until 100
    mutex_lock(&mtx, OS_FOREVER);
    while ((int32_t)(os_ticks() - 100u) < 0) {
        if (os_curr->prio < l_min_prio)
            l_min_prio = os_curr->prio; // lowest number = highest priority seen
    }
    mutex_unlock(&mtx);
    l_prio_after = os_curr->prio;

    // Timeouts: hold mtx2 while asleep, H gives up waiting on it
    sleep_until(400);
    mutex_lock(&mtx2, OS_FOREVER);
    os_sleep(100);
    mutex_unlock(&mtx2);
    l_prio_after2 = os_curr->prio;

    // Semaphore wake-up
    sleep_until(650);
    post_done = 0;
    sem_post(&sem_go);                  // H is higher priority: it should run right here
    post_done = 1;

    while (1)
        os_sleep(1000);
}

int main(void)
{
    SYSCTL_RCGCGPIO_R |= 0x20;          // clock Port F
    while ((SYSCTL_PRGPIO_R & 0x20) == 0) {
    }
    GPIO_PORTF_DIR_R |= LED_RED | LED_BLUE | LED_GREEN;
    GPIO_PORTF_DEN_R |= LED_RED | LED_BLUE | LED_GREEN;
    GPIO_PORTF_DATA_R = LED_BLUE;       // running

    mutex_init(&mtx);
    mutex_init(&mtx2);
    sem_init(&sem_never, 0, 1);
    sem_init(&sem_go, 0, 1);

    os_init();
    os_task_create(h_task, NULL, "H", PRIO_H, stack_h, 256);
    os_task_create(m_task, NULL, "M", PRIO_M, stack_m, 256);
    os_task_create(l_task, NULL, "L", PRIO_L, stack_l, 256);
    os_start();                         // never returns

    return 0;
}

#endif /* SYNC_TEST */

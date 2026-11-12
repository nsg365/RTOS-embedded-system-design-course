/*
 * main.c - Phase 2 test: priorities, os_sleep and the idle task.
 * (The phase 1 round-robin test is kept in the git tag "phase1".)
 *
 * Three tasks run the same function. Each shows its own colour on the RGB LED
 * (and holds its probe pin high) while it is running:
 *   green, priority 1 (highest): runs 200 ms, then sleeps 800 ms -> once a second
 *   red,   priority 2          : never sleeps (TEST_IDLE 0)
 *   blue,  priority 2          : never sleeps (TEST_IDLE 0)
 * Red and blue share a priority, so they take turns every time slice (10 ms,
 * looks purple). Green has the higher priority, so the moment its sleep ends it
 * takes the CPU from them, even though they never give it up themselves.
 *
 * TEST_IDLE 1: red and blue also sleep (100 ms on, 400 ms off). When all three
 * are asleep, the idle task runs and the LED goes dark.
 *
 * What to check:
 *   TEST_IDLE 0:
 *   - Purple, with a 200 ms green flash exactly once a second.
 *   - Scope: PB2 (green) high 200 ms every 1000 ms; PB0/PB1 stay low while PB2 is high.
 *   - Expressions view: os_idle_ticks stays 0 (some task is always ready).
 *   TEST_IDLE 1:
 *   - LED dark most of the time, with short green / red / blue flashes.
 *   - os_idle_ticks rises (idle runs whenever all three tasks are asleep).
 *   - White LED (all three on) = HardFault; read os_fault_pc and look it up in RTOS.map.
 */
#include <stdint.h>
#include "tm4c123gh6pm.h"
#include "os.h"

#ifndef UART_TEST                       // uart_test.c replaces this test (and its main())

#define TEST_IDLE   0                   // 1: red and blue sleep too, so idle gets to run

#define LED_RED     0x02u               // PF1
#define LED_BLUE    0x04u               // PF2
#define LED_GREEN   0x08u               // PF3
#define PROBE_RED   0x01u               // PB0
#define PROBE_BLUE  0x02u               // PB1
#define PROBE_GREEN 0x04u               // PB2

typedef struct {
    uint32_t led;                       // Port F LED bit on while running
    uint32_t probe;                     // Port B pin held high while running
    uint32_t run_ms;                    // how long to run each cycle (0 = forever)
    uint32_t sleep_ms;                  // how long to sleep after that
} task_cfg_t;

static const task_cfg_t cfg_green = { LED_GREEN, PROBE_GREEN, 200u, 800u };
#if TEST_IDLE
static const task_cfg_t cfg_red   = { LED_RED,   PROBE_RED,   100u, 400u };
static const task_cfg_t cfg_blue  = { LED_BLUE,  PROBE_BLUE,  100u, 400u };
#else
static const task_cfg_t cfg_red   = { LED_RED,   PROBE_RED,   0u,   0u };
static const task_cfg_t cfg_blue  = { LED_BLUE,  PROBE_BLUE,  0u,   0u };
#endif

static uint32_t stack_green[256];
static uint32_t stack_red[256];
static uint32_t stack_blue[256];

static void gpio_init(void)
{
    SYSCTL_RCGCGPIO_R |= 0x22;          // clock Port B and Port F
    while ((SYSCTL_PRGPIO_R & 0x22) != 0x22) {
    }
    GPIO_PORTF_DIR_R |= LED_RED | LED_BLUE | LED_GREEN;
    GPIO_PORTF_DEN_R |= LED_RED | LED_BLUE | LED_GREEN;
    GPIO_PORTF_DATA_R &= ~(LED_RED | LED_BLUE | LED_GREEN);

    GPIO_PORTB_DIR_R |= PROBE_RED | PROBE_BLUE | PROBE_GREEN;
    GPIO_PORTB_DEN_R |= PROBE_RED | PROBE_BLUE | PROBE_GREEN;
    GPIO_PORTB_DATA_R &= ~(PROBE_RED | PROBE_BLUE | PROBE_GREEN);
}

static void led_task(void *arg)
{
    const task_cfg_t *cfg = (const task_cfg_t *)arg;
    uint32_t start;

    while (1) {
        /* Run for run_ms. Whole-port writes: this task's LED and probe on, the
           others off. They are rewritten on every pass, so they come straight
           back on when this task is resumed after being preempted. */
        start = os_ticks();
        while (cfg->run_ms == 0 || os_ticks() - start < cfg->run_ms) {
            GPIO_PORTF_DATA_R = cfg->led;
            GPIO_PORTB_DATA_R = cfg->probe;
        }

        // LED and probe off, then sleep: the CPU goes to another task (or idle)
        GPIO_PORTF_DATA_R = 0;
        GPIO_PORTB_DATA_R = 0;
        os_sleep(cfg->sleep_ms);
    }
}

int main(void)
{
    gpio_init();

    os_init();
    os_task_create(led_task, (void *)&cfg_green, "green", 1, stack_green, 256);
    os_task_create(led_task, (void *)&cfg_red,   "red",   2, stack_red,   256);
    os_task_create(led_task, (void *)&cfg_blue,  "blue",  2, stack_blue,  256);
    os_start();                         // never returns

    return 0;
}
#endif

/*
 * main.c - Phase 1 test: kernel bring-up.
 *
 * Two tasks run the SAME function with different arguments, so each needs
 * its own stack and context. Neither task ever gives up the CPU, so the only
 * way both get to run is preemptive round-robin switching.
 *
 * Each task keeps ITS LED on (and the other one off) while it runs, so the
 * LED colour shows which task is running right now: red = blinkA, blue = blinkB.
 *
 * What to check:
 *   - OS_TIME_SLICE 10 (normal): the LED switches every 10 ms, too fast to see,
 *     so it looks steady purple. Set OS_TIME_SLICE to 500 in os.h to watch it
 *     alternate red / blue every 0.5 s, then set it back to 10.
 *   - Scope on PB0 and PB1: each pin is high while its task is running.
 *     Expect complementary square waves, 10 ms high / 10 ms low (the time slice).
 *   - Expressions view: os_switch_count rises by about 100 per second.
 *   - White LED (all three on) = HardFault; read os_fault_pc and look it up in RTOS.map.
 */
#include <stdint.h>
#include "tm4c123gh6pm.h"
#include "os.h"

#define LED_RED     0x02u               // PF1
#define LED_BLUE    0x04u               // PF2
#define PROBE_A     0x01u               // PB0
#define PROBE_B     0x02u               // PB1

typedef struct {
    uint32_t led;                       // Port F LED bit on while running
    uint32_t probe;                     // Port B pin held high while running
} blink_cfg_t;

static const blink_cfg_t cfg_a = { LED_RED,  PROBE_A };
static const blink_cfg_t cfg_b = { LED_BLUE, PROBE_B };

static uint32_t stack_a[256];
static uint32_t stack_b[256];

static void gpio_init(void)
{
    SYSCTL_RCGCGPIO_R |= 0x22;          /* clock Port B and Port F */
    while ((SYSCTL_PRGPIO_R & 0x22) != 0x22) {
    }
    GPIO_PORTF_DIR_R |= LED_RED | LED_BLUE | 0x08u;
    GPIO_PORTF_DEN_R |= LED_RED | LED_BLUE | 0x08u;
    GPIO_PORTF_DATA_R &= ~(LED_RED | LED_BLUE | 0x08u);

    GPIO_PORTB_DIR_R |= PROBE_A | PROBE_B;
    GPIO_PORTB_DEN_R |= PROBE_A | PROBE_B;
    GPIO_PORTB_DATA_R &= ~(PROBE_A | PROBE_B);
}

static void blink_task(void *arg)
{
    const blink_cfg_t *cfg = (const blink_cfg_t *)arg;

    while (1) {
        /* Whole-port writes: this task's LED and probe pin on, the other
           task's off. PF0/PF4 are inputs, so writing them has no effect. */
        GPIO_PORTF_DATA_R = cfg->led;
        GPIO_PORTB_DATA_R = cfg->probe;
    }
}

int main(void)
{
    gpio_init();

    os_init();
    os_task_create(blink_task, (void *)&cfg_a, "blinkA", 1, stack_a, 256);
    os_task_create(blink_task, (void *)&cfg_b, "blinkB", 1, stack_b, 256);
    os_start();                         /* never returns */

    return 0;
}

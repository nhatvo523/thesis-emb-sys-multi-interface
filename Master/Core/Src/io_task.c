/**
 * @file    io_task.c
 * @brief   IO task — software-debounced digital inputs (PB4–PB7) and
 *          GPIO relay output control.
 *
 * Architecture
 * ------------
 * The task runs at a fixed 10 ms tick (osDelay(IO_POLL_MS)).  Each tick it
 * samples the four input pins and feeds them through a 3-consecutive-sample
 * majority filter (debounce).  When the stable di_state changes it updates
 * g_appData.di_state and sets EVT_IO_CHANGED.
 *
 * Relay outputs are driven from g_appData.mb_coil[0..3] whenever the task
 * receives EVT_RELAY_CMD from g_systemEvents.  The current relay state is
 * written back to g_appData.relay_state so the USB log can report it.
 *
 * Pin mapping (INPUT PULLUP — logic inverted: LOW = active/1)
 * -----------------------------------------------------------
 *   PB4  → di_state bit 0
 *   PB5  → di_state bit 1
 *   PB6  → di_state bit 2
 *   PB7  → di_state bit 3
 *
 * Relay output mapping (OUTPUT PUSHPULL — HIGH = relay energised)
 * ---------------------------------------------------------------
 *   PB8  → relay bit 0   (mb_coil[0])   RELAY1
 *   PB9  → relay bit 1   (mb_coil[1])   RELAY2
 *   PE0  → relay bit 2   (mb_coil[2])   RELAY3
 *   PE1  → relay bit 3   (mb_coil[3])   RELAY4
 *
 * Pins are defined via RELAY1..4_PORT/PIN macros in main.h.
 */

#include "io_task.h"
#include "app_data.h"
#include "main.h"
#include "sse_server.h"         /* SSE_Push_Snapshot()                       */
#include "cmsis_os.h"
#include "FreeRTOS.h"           /* pvPortMalloc / vPortFree                  */
#include "stm32f4xx_hal.h"

/* ===========================================================================
 * Private constants
 * ========================================================================= */

/** Task poll interval in milliseconds (also the debounce sample period). */
#define IO_POLL_MS          10U

/**
 * Number of consecutive identical samples required to accept a new input
 * state.  At IO_POLL_MS = 10 ms this gives 30 ms debounce time.
 */
#define DI_DEBOUNCE_COUNT   3U

/** Number of monitored digital inputs. */
#define DI_COUNT            4U

/** Number of relay outputs. */
#define RELAY_COUNT         4U

/* ---------------------------------------------------------------------------
 * Digital input pin table
 * -------------------------------------------------------------------------*/

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t      pin;
} GpioPin_t;

static const GpioPin_t s_di_pins[DI_COUNT] = {
    { GPIOB, GPIO_PIN_4 },   /* bit 0 */
    { GPIOB, GPIO_PIN_5 },   /* bit 1 */
    { GPIOB, GPIO_PIN_6 },   /* bit 2 */
    { GPIOB, GPIO_PIN_7 },   /* bit 3 */
};

static const GpioPin_t s_relay_pins[RELAY_COUNT] = {
    { RELAY1_PORT, RELAY1_PIN },   /* bit 0 — mb_coil[0] — PB8 */
    { RELAY2_PORT, RELAY2_PIN },   /* bit 1 — mb_coil[1] — PB9 */
    { RELAY3_PORT, RELAY3_PIN },   /* bit 2 — mb_coil[2] — PE0 */
    { RELAY4_PORT, RELAY4_PIN },   /* bit 3 — mb_coil[3] — PE1 */
};

/* ===========================================================================
 * Private helpers
 * ========================================================================= */

/**
 * @brief  Sample all DI pins into a 4-bit mask.
 *
 * PB4–PB7 are INPUT PULLUP: GPIO_PIN_RESET means the external contact is
 * closed (active), so we invert the raw reading.
 *
 * @return  4-bit mask: bit N = 1 → input N is active.
 */
static uint8_t sample_di(void)
{
    uint8_t mask = 0U;
    for (uint8_t i = 0U; i < DI_COUNT; i++)
    {
        if (HAL_GPIO_ReadPin(s_di_pins[i].port, s_di_pins[i].pin) == GPIO_PIN_RESET)
        {
            mask |= (uint8_t)(1U << i);
        }
    }
    return mask;
}

/**
 * @brief  Drive relay GPIO pins from a 4-bit relay mask.
 *
 * @param  relay_mask  Bit N = 1 → energise relay N (HIGH output).
 */
static void apply_relay(uint8_t relay_mask)
{
    for (uint8_t i = 0U; i < RELAY_COUNT; i++)
    {
        GPIO_PinState state = ((relay_mask >> i) & 0x01U) ? GPIO_PIN_SET
                                                           : GPIO_PIN_RESET;
        HAL_GPIO_WritePin(s_relay_pins[i].port, s_relay_pins[i].pin, state);
    }
}

/**
 * @brief  Read mb_coil[0..3] under mutex and build relay mask.
 *
 * @return  4-bit relay mask derived from Modbus coil values.
 */
static uint8_t relay_mask_from_coils(void)
{
    uint8_t mask = 0U;
    if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
    {
        for (uint8_t i = 0U; i < RELAY_COUNT; i++)
        {
            if (g_appData.mb_coil[i])
            {
                mask |= (uint8_t)(1U << i);
            }
        }
        osMutexRelease(g_dataMutex);
    }
    return mask;
}

/* ===========================================================================
 * Public API
 * ========================================================================= */

void IO_Task_Init(void)
{
    /* No static resources to allocate; GPIO clocks and modes are configured
     * by MX_GPIO_Init() in main.c before the scheduler starts. */
}

void IO_Task_Run(void *argument)
{
    (void)argument;

    /*
     * Debounce state per channel.
     * sample_buf holds the last DI_DEBOUNCE_COUNT raw samples (ring buffer).
     * stable_state is the currently accepted, debounced value.
     */
    uint8_t sample_buf[DI_DEBOUNCE_COUNT] = { 0U };
    uint8_t buf_idx    = 0U;
    uint8_t stable_di  = 0U;   /* last accepted DI state */
    uint8_t relay_cur  = 0U;   /* last applied relay mask */

    /* Warm-up: fill the sample buffer before making the first decision. */
    for (uint8_t i = 0U; i < DI_DEBOUNCE_COUNT; i++)
    {
        sample_buf[i] = sample_di();
        osDelay(IO_POLL_MS);
    }

    /* Accept the initial state without raising EVT_IO_CHANGED. */
    stable_di = sample_buf[0];

    /* Synchronise relay outputs with current coil state at startup. */
    relay_cur = relay_mask_from_coils();
    apply_relay(relay_cur);

    if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
    {
        g_appData.di_state    = stable_di;
        g_appData.relay_state = relay_cur;
        osMutexRelease(g_dataMutex);
    }

    /* -----------------------------------------------------------------------
     * Main loop
     * ---------------------------------------------------------------------- */
    for (;;)
    {
        osDelay(IO_POLL_MS);

        /* -----------------------------------------------------------------
         * 1. Sample DI pins and update ring buffer
         * ---------------------------------------------------------------- */
        sample_buf[buf_idx] = sample_di();
        buf_idx = (uint8_t)((buf_idx + 1U) % DI_DEBOUNCE_COUNT);

        /* Debounce: accept new state only if all DI_DEBOUNCE_COUNT samples
         * agree on the same value. */
        uint8_t all_same = 1U;
        uint8_t candidate = sample_buf[0];
        for (uint8_t i = 1U; i < DI_DEBOUNCE_COUNT; i++)
        {
            if (sample_buf[i] != candidate)
            {
                all_same = 0U;
                break;
            }
        }

        if (all_same && (candidate != stable_di))
        {
            stable_di = candidate;

            /* Allocate snapshot before locking (pvPortMalloc is safe outside mutex). */
            AppData_t *snap = (AppData_t *)pvPortMalloc(sizeof(AppData_t));

            /* Write debounced DI state under mutex; copy snapshot inside same lock. */
            if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
            {
                g_appData.di_state = stable_di;
                if (snap != NULL) { *snap = g_appData; }  /* copy inside same lock */
                osMutexRelease(g_dataMutex);
            }

            /* Notify USB task (and Modbus task if it ever needs DI). */
            xEventGroupSetBits(g_systemEvents, EVT_IO_CHANGED);

            /* Push snapshot directly — no second mutex needed. */
            SSE_Push_Snapshot(snap);
        }

        /* -----------------------------------------------------------------
         * 2. Handle relay command from web server CGI or ESP32 (EVT_RELAY_CMD)
         *    — non-blocking poll.
         * ---------------------------------------------------------------- */
        EventBits_t bits = xEventGroupWaitBits(
            g_systemEvents,
            EVT_RELAY_CMD,
            pdTRUE,          /* clear matched bits on exit */
            pdFALSE,         /* wake on any bit            */
            0U               /* no block — just poll       */
        );

        if (bits & EVT_RELAY_CMD)
        {
            uint8_t new_relay = relay_mask_from_coils();

            if (new_relay != relay_cur)
            {
                relay_cur = new_relay;
                apply_relay(relay_cur);

                /* Allocate snapshot before locking. */
                AppData_t *snap = (AppData_t *)pvPortMalloc(sizeof(AppData_t));

                /* Write relay_state under mutex; copy snapshot inside same lock. */
                if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
                {
                    g_appData.relay_state = relay_cur;
                    if (snap != NULL) { *snap = g_appData; }  /* copy inside same lock */
                    osMutexRelease(g_dataMutex);
                }

                /* Signal USB task so it logs the relay change. */
                xEventGroupSetBits(g_systemEvents, EVT_IO_CHANGED);

                /* Push snapshot directly — no second mutex needed. */
                SSE_Push_Snapshot(snap);
            }
        }
    }
}

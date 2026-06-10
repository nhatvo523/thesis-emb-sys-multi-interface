/**
 * @file    modbus_task.c
 * @brief   Modbus RTU master — USART1, 9600 8N1.
 *          Polls MB_SLAVE_COUNT slaves sequentially using FC 0x03
 *          (Read Holding Registers).
 *
 * Stack   : 2 kB  (Task_Modbus_attributes.stack_size = 512 * 4)
 * Priority: osPriorityLow
 *
 * Poll sequence (repeats forever)
 * --------------------------------
 *  For each slave index i = 0, 1:
 *    1. Build FC 0x03 request for slave address (i + 1).
 *    2. Arm USART1 DMA receiver (s_rx_buf).
 *    3. Transmit request (blocking HAL_UART_Transmit — 8 bytes ≈ 8.3 ms
 *       at 9600 baud; DMA RX is armed AFTER TX so the TX frame itself
 *       never enters the RX buffer).
 *    4. Block on ulTaskNotifyTake() — USART1 IDLE ISR fires when the
 *       slave's response has been fully clocked in and the line goes idle.
 *    5. Stop DMA, measure byte count.
 *    6. Validate: length ≥ expected, address byte, function code, CRC-16.
 *    7a. CRC / timeout error →
 *           LOCK mutex → mb_slave[i].connected=0, mb_err_count++ → UNLOCK
 *    7b. Valid response →
 *           extract registers into local array (no mutex needed here)
 *           LOCK mutex → copy local array into mb_slave[i].holding_reg[],
 *                         mb_slave[i].connected=1, mb_rx_count++ → UNLOCK
 *           xEventGroupSetBits(EVT_MODBUS_RX)   ← after UNLOCK
 *    8. osDelay(MB_INTER_SLAVE_MS) before polling next slave.
 *  osDelay(MB_CYCLE_MS) after the full poll cycle.
 *
 * Mutex discipline
 * ----------------
 * g_dataMutex is acquired exactly ONCE per slave per cycle, and only AFTER
 * the complete response has been received and validated.  The TX frame
 * building and CRC calculation are done entirely from local variables —
 * no shared data is read during those phases.
 *
 * IDLE ISR
 * --------
 * Modbus_UART_IdleCallback() is called from USART1_IRQHandler in
 * stm32f4xx_it.c after clearing the IDLE flag.  It calls
 * vTaskNotifyGiveFromISR() to unblock the task.
 */

#include "modbus_task.h"
#include "app_data.h"           /* g_appData, g_dataMutex, EVT_MODBUS_RX    */
#include "sse_server.h"         /* SSE_Push_Snapshot()                       */
#include "cmsis_os.h"           /* osMutexAcquire/Release, osDelay           */
#include "FreeRTOS.h"           /* TaskHandle_t, ulTaskNotifyTake            */
#include "task.h"               /* xTaskGetCurrentTaskHandle                 */
#include "stm32f4xx_hal.h"      /* UART, DMA HAL                             */

/* ===========================================================================
 * External HAL handles
 * ========================================================================= */
extern UART_HandleTypeDef huart1;

/* ===========================================================================
 * Private constants
 * ========================================================================= */

/** Slave addresses indexed by MB_SLAVE_COUNT (index 0 → slave addr 1, etc.). */
static const uint8_t s_slave_addr[MB_SLAVE_COUNT] = { 1U, 2U };

/** First holding register to read from each slave. */
#define MB_POLL_START_REG       0U

/** FC 0x03 request frame length is always 8 bytes. */
#define MB_REQUEST_LEN          8U

/**
 * Expected FC 0x03 response length for MB_SLAVE_REG_COUNT registers.
 * Format: [addr][0x03][byte_count][data × MB_SLAVE_REG_COUNT × 2][CRC × 2]
 */
#define MB_RESPONSE_LEN         (3U + (MB_SLAVE_REG_COUNT) * 2U + 2U)

/** Maximum raw buffer size (full Modbus frame upper bound). */
#define MB_FRAME_MAX            256U

/** How long to wait for a slave response before declaring a timeout. */
#define MB_RESPONSE_TIMEOUT_MS  50U

/** Pause between transmitting to slave N and starting slave N+1. */
#define MB_INTER_SLAVE_MS       10U

/** Pause after the full poll cycle (both slaves) before restarting. */
#define MB_CYCLE_MS             90U

/* ===========================================================================
 * Private state
 * ========================================================================= */

static uint8_t      s_rx_buf[MB_FRAME_MAX];
static TaskHandle_t s_task_handle = NULL;

/* ===========================================================================
 * CRC-16 (Modbus polynomial 0xA001)
 * ========================================================================= */

/**
 * @brief  Compute Modbus CRC-16 (bit-by-bit, no lookup table).
 */
static uint16_t Modbus_CRC16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    for (uint16_t i = 0U; i < len; i++)
    {
        crc ^= (uint16_t)buf[i];
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            crc = (crc & 0x0001U) ? ((crc >> 1U) ^ 0xA001U) : (crc >> 1U);
        }
    }
    return crc;
}

/* ===========================================================================
 * Private: build FC 0x03 request frame
 * ========================================================================= */

/**
 * @brief  Fill @p frame with a FC 0x03 request and return its length (8).
 *
 * @param  frame      Output buffer, must be at least MB_REQUEST_LEN bytes.
 * @param  slave_addr Modbus slave address byte.
 * @param  start_reg  First holding register to read (0-based).
 * @param  reg_count  Number of registers to read.
 * @return MB_REQUEST_LEN (8).
 */
static uint16_t build_fc03_request(uint8_t  *frame,
                                    uint8_t   slave_addr,
                                    uint16_t  start_reg,
                                    uint16_t  reg_count)
{
    frame[0] = slave_addr;
    frame[1] = 0x03U;                              /* FC Read Holding Regs  */
    frame[2] = (uint8_t)(start_reg >> 8U);
    frame[3] = (uint8_t)(start_reg & 0xFFU);
    frame[4] = (uint8_t)(reg_count >> 8U);
    frame[5] = (uint8_t)(reg_count & 0xFFU);

    uint16_t crc = Modbus_CRC16(frame, 6U);
    frame[6] = (uint8_t)(crc & 0xFFU);            /* CRC low byte          */
    frame[7] = (uint8_t)((crc >> 8U) & 0xFFU);   /* CRC high byte         */

    return MB_REQUEST_LEN;
}

/* ===========================================================================
 * Private: UART recovery and slave error reporting
 * ========================================================================= */

/**
 * @brief  Recover huart1 after a HAL error or BUSY condition.
 *
 * HAL_UART_Abort() resets both gState and RxState to HAL_UART_STATE_READY
 * and stops any in-progress DMA transfer — safe to call even if idle.
 * The IDLE interrupt is explicitly re-enabled afterwards because certain
 * HAL versions may clear CR1.IDLEIE during the abort sequence.
 */
static void uart_recover(void)
{
    HAL_UART_Abort(&huart1);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
}

/**
 * @brief  Mark slave @p slave_idx disconnected, record the specific error
 *         code, and push an SSE update.
 *
 * Drains any pending task notification first (guards against a late IDLE
 * ISR arriving after a HAL failure or after the response timeout).
 * All shared-state writes are protected by g_dataMutex.  Other tasks are
 * not blocked beyond the mutex acquisition window.
 * EVT_MODBUS_ERR is fired AFTER the mutex is released so listeners can
 * read the updated mb_last_err immediately.
 *
 * @param  slave_idx  Index into mb_slave[] (0 or 1).
 * @param  code       Specific failure reason (MbErrCode_t).
 */
static void slave_mark_error(uint8_t slave_idx, MbErrCode_t code)
{
    /* Drain any stale notification so the next poll cycle is not fooled. */
    ulTaskNotifyTake(pdTRUE, 0U);

    AppData_t *snap = (AppData_t *)pvPortMalloc(sizeof(AppData_t));
    if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
    {
        g_appData.mb_slave[slave_idx].connected   = 0U;
        g_appData.mb_slave[slave_idx].mb_last_err = code;
        g_appData.mb_err_count++;
        if (snap != NULL) { *snap = g_appData; }
        osMutexRelease(g_dataMutex);

        /* Notify USB task of the specific error — strictly after mutex release. */
        xEventGroupSetBits(g_systemEvents, EVT_MODBUS_ERR);
        SSE_Push_Snapshot(snap);
    }
    else
    {
        /* Mutex acquire timed out — another task is holding the lock.
         * Do NOT update shared state (unsafe) and do NOT push SSE.
         * Free the snapshot to avoid a heap leak; the error will be
         * reflected in the next successful cycle. */
        vPortFree(snap);
    }
}

/* ===========================================================================
 * Private: poll one slave
 * ========================================================================= */

/**
 * @brief  Execute one full request → response cycle for slave @p slave_idx.
 *
 * Steps:
 *   1. Build FC 0x03 request (local frame, no mutex).
 *   2. Arm USART1 DMA receiver.
 *   3. Transmit request (blocking — DMA RX is active, TX is polling mode).
 *   4. Wait for IDLE ISR notification (response complete or timeout).
 *   5. Stop DMA, measure byte count.
 *   6. Validate response; on any error:
 *        LOCK → connected=0, mb_err_count++ → UNLOCK → return.
 *   7. Extract registers into local array (no mutex, working from s_rx_buf).
 *   8. LOCK → copy local array to mb_slave[slave_idx], connected=1,
 *             mb_rx_count++ → UNLOCK.
 *   9. xEventGroupSetBits(EVT_MODBUS_RX) — after UNLOCK.
 *
 * @param  slave_idx  Index into s_slave_addr[] (0 or 1).
 */
static void poll_one_slave(uint8_t slave_idx)
{
    uint8_t  req[MB_REQUEST_LEN];
    uint8_t  addr = s_slave_addr[slave_idx];

    /* ------------------------------------------------------------------
     * 0. Drain any stale task notification accumulated during the
     *    inter-slave delay (osDelay) or from a late response of the
     *    previous slave.  Must be done BEFORE arming DMA so that
     *    ulTaskNotifyTake() in step 4 only wakes on THIS slave's IDLE ISR.
     *    Timeout = 0 → non-blocking; only consumes a pending notification.
     * ---------------------------------------------------------------- */
    ulTaskNotifyTake(pdTRUE, 0U);

    /* ------------------------------------------------------------------
     * 1. Build request frame entirely from local variables — no mutex.
     * ---------------------------------------------------------------- */
    build_fc03_request(req, addr,
                       MB_POLL_START_REG,
                       (uint16_t)MB_SLAVE_REG_COUNT);

    /* ------------------------------------------------------------------
     * 2. Arm DMA receiver BEFORE transmitting so the slave's response is
     *    captured from the first byte.  HAL_UART_Transmit (polling TX)
     *    does not interfere with DMA RX — they use independent streams.
     *
     *    HAL_BUSY   → UART handle stuck from a previous incomplete transfer;
     *                 uart_recover() resets it and we skip this slave.
     *    HAL_ERROR  → DMA or peripheral fault; same recovery path.
     *    In both cases other tasks are unaffected — we return immediately
     *    and the caller applies the normal inter-slave delay.
     * ---------------------------------------------------------------- */
    if (HAL_UART_Receive_DMA(&huart1, s_rx_buf, MB_FRAME_MAX) != HAL_OK)
    {
        uart_recover();
        slave_mark_error(slave_idx, MB_ERR_HAL_RX);
        return;
    }

    /* ------------------------------------------------------------------
     * 3. Send request — blocking (8 bytes ≈ 8.3 ms at 9600 baud).
     *    Using polling TX here is intentional: it keeps the flow linear
     *    and avoids a second task-notification channel for TX complete.
     *
     *    On TX failure (HAL_BUSY / HAL_TIMEOUT / HAL_ERROR): stop the DMA
     *    that was armed in step 2, recover the UART, then skip this slave.
     * ---------------------------------------------------------------- */
    if (HAL_UART_Transmit(&huart1, req, MB_REQUEST_LEN, 20U) != HAL_OK)
    {
        HAL_UART_DMAStop(&huart1);
        uart_recover();
        slave_mark_error(slave_idx, MB_ERR_HAL_TX);
        return;
    }

    /* ------------------------------------------------------------------
     * 4. Wait for IDLE ISR → slave response is completely received.
     *    ulTaskNotifyTake blocks the task; CPU is free for other tasks.
     * ---------------------------------------------------------------- */
    uint32_t notified = ulTaskNotifyTake(pdTRUE,
                                         pdMS_TO_TICKS(MB_RESPONSE_TIMEOUT_MS));

    /* ------------------------------------------------------------------
     * 5. Stop DMA and measure how many bytes the slave sent.
     * ---------------------------------------------------------------- */
    HAL_UART_DMAStop(&huart1);
    uint16_t rx_len = (uint16_t)(MB_FRAME_MAX -
                      (uint16_t)__HAL_DMA_GET_COUNTER(huart1.hdmarx));

    /* ------------------------------------------------------------------
     * 6. Validate — all checks done BEFORE acquiring the mutex so the
     *    lock is held only for the actual data write.
     * ---------------------------------------------------------------- */
    MbErrCode_t err_code = MB_ERR_NONE;

    if (notified == 0U)
    {
        err_code = MB_ERR_TIMEOUT;      /* timeout — slave did not respond */
    }
    else if (rx_len < MB_RESPONSE_LEN)
    {
        err_code = MB_ERR_SHORT_FRAME;  /* response too short */
    }
    else if (s_rx_buf[0] != addr)
    {
        err_code = MB_ERR_BAD_ADDR;     /* wrong slave address in response */
    }
    else if (s_rx_buf[1] != 0x03U)
    {
        err_code = MB_ERR_BAD_FC;       /* unexpected FC (could be exception 0x83) */
    }
    else
    {
        uint16_t calc_crc = Modbus_CRC16(s_rx_buf, rx_len - 2U);
        uint16_t recv_crc = (uint16_t)s_rx_buf[rx_len - 2U] |
                            ((uint16_t)s_rx_buf[rx_len - 1U] << 8U);
        if (calc_crc != recv_crc)
        {
            err_code = MB_ERR_CRC;      /* CRC mismatch */
        }
    }

    if (err_code != MB_ERR_NONE)
    {
        /* slave_mark_error() drains any stale late notification, records
         * the specific fault code, fires EVT_MODBUS_ERR, and pushes SSE. */
        slave_mark_error(slave_idx, err_code);
        return;
    }

    /* ------------------------------------------------------------------
     * 7. Response is fully validated.  Extract registers into a local
     *    array — still no mutex, working entirely from s_rx_buf which
     *    only this task writes.
     *
     *    FC 0x03 response layout:
     *      [addr][0x03][byte_count][reg0_hi][reg0_lo] … [CRC_lo][CRC_hi]
     *    Register N starts at byte offset 3 + N*2.
     * ---------------------------------------------------------------- */
    uint16_t local_regs[MB_SLAVE_REG_COUNT];
    for (uint8_t i = 0U; i < (uint8_t)MB_SLAVE_REG_COUNT; i++)
    {
        local_regs[i] = ((uint16_t)s_rx_buf[3U + (uint16_t)i * 2U] << 8U)
                       | (uint16_t)s_rx_buf[4U + (uint16_t)i * 2U];
    }

    /* ------------------------------------------------------------------
     * 8. Single critical section:
     *       - copy local_regs[] → g_appData.mb_slave[slave_idx].holding_reg[]
     *       - mark slave connected
     *       - reset error code to MB_ERR_NONE (recovery)
     *       - increment mb_rx_count
     *       - snapshot g_appData for SSE push (inside same lock)
     *    This is the ONLY place g_appData is written for this slave in
     *    this cycle.  No other task sees a partially-written slave entry.
     * ---------------------------------------------------------------- */
    AppData_t *snap = (AppData_t *)pvPortMalloc(sizeof(AppData_t));
    if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
    {
        for (uint8_t i = 0U; i < (uint8_t)MB_SLAVE_REG_COUNT; i++)
        {
            g_appData.mb_slave[slave_idx].holding_reg[i] = local_regs[i];
        }
        g_appData.mb_slave[slave_idx].connected = 1U;
        g_appData.mb_slave[slave_idx].mb_last_err = MB_ERR_NONE;  /* Recovery: clear error */
        g_appData.mb_rx_count++;
        if (snap != NULL) { *snap = g_appData; }  /* copy inside same lock */
        osMutexRelease(g_dataMutex);
    }

    /* ------------------------------------------------------------------
     * 9. Notify USB and IO tasks — strictly after the mutex is released.
     * ---------------------------------------------------------------- */
    xEventGroupSetBits(g_systemEvents, EVT_MODBUS_RX);

    /* Push snapshot directly to SSE — no second mutex needed. */
    SSE_Push_Snapshot(snap);
}

/* ===========================================================================
 * UART IDLE line ISR callback
 * ========================================================================= */

/**
 * @brief  Unblock the Modbus task from the USART1 IDLE ISR.
 *
 * Called from USART1_IRQHandler in stm32f4xx_it.c:
 *
 *   if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE)) {
 *       __HAL_UART_CLEAR_IDLEFLAG(&huart1);
 *       Modbus_UART_IdleCallback();
 *   }
 *   HAL_UART_IRQHandler(&huart1);
 */
void Modbus_UART_IdleCallback(void)
{
    if (s_task_handle == NULL)
    {
        return;
    }
    BaseType_t higher_prio_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task_handle, &higher_prio_woken);
    portYIELD_FROM_ISR(higher_prio_woken);
}

/* ===========================================================================
 * Public API
 * ========================================================================= */

void Modbus_Task_Init(void)
{
    /* Enable UART IDLE interrupt so the ISR can signal end-of-frame. */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
}

void Modbus_Task_Run(void *argument)
{
    (void)argument;

    s_task_handle = xTaskGetCurrentTaskHandle();

    for (;;)
    {
        /* Poll each slave in order: slave 0 (addr=1) then slave 1 (addr=2). */
        for (uint8_t i = 0U; i < (uint8_t)MB_SLAVE_COUNT; i++)
        {
            poll_one_slave(i);
            osDelay(MB_INTER_SLAVE_MS);
        }

        /* Pause before the next full poll cycle. */
        osDelay(MB_CYCLE_MS);
    }
}

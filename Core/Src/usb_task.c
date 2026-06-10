/**
 * @file    usb_task.c
 * @brief   USB CDC log task — formats and transmits gateway status over the
 *          USB virtual COM port in response to inter-task events.
 *
 * Stack   : 2 kB  (Task_USB_attributes.stack_size = 512 * 4)
 * Priority: osPriorityLow
 *
 * Architecture
 * ------------
 * The task is purely reactive: it blocks indefinitely on g_systemEvents and
 * wakes only when another task sets one of the three event bits.  All data is
 * read from g_appData under g_dataMutex into a stack-local snapshot before
 * formatting, so the mutex is held for the minimum possible time.
 *
 * USB transmission
 * ----------------
 * CDC_Transmit_FS() can return USBD_BUSY if the previous transfer has not
 * completed.  The internal log_usb() helper retries up to LOG_RETRY_MAX
 * times with a LOG_RETRY_DELAY_MS pause between attempts so no log line is
 * silently dropped under light host load.
 *
 * Log format examples
 * -------------------
 *   === STM32 Gateway Boot ===
 *   [NET] IP   : 192.168.1.105
 *   [NET] Mask : 255.255.255.0
 *   [NET] GW   : 192.168.1.1
 *   [NET] Web  : http://192.168.1.105
 *   [MB]  RX:1  ERR:0  HR[0]=0x0000 HR[1]=0x0000 HR[2]=0x0000 HR[3]=0x0000
 *   [IO]  DI   : 0b00001010  Relay: 0b00000011
 */

#include "usb_task.h"
#include "app_data.h"           /* g_appData, g_dataMutex, g_systemEvents    */
#include "usb_device.h"         /* MX_USB_DEVICE_Init()                      */
#include "usbd_cdc_if.h"        /* CDC_Transmit_FS()                         */
#include "cmsis_os.h"           /* osDelay(), osMutexAcquire/Release         */
#include "main.h"               /* hiwdg — kick IWDG watchdog                */
#include <stdio.h>              /* snprintf()                                */
#include <string.h>             /* strlen()                                  */

/* ===========================================================================
 * Private constants
 * ========================================================================= */

/** Wait time after USB device init for the host to enumerate the port. */
#define USB_ENUM_DELAY_MS       2000U

/** Maximum number of CDC_Transmit_FS retries when USBD_BUSY is returned. */
#define LOG_RETRY_MAX           10U

/** Delay between retries in milliseconds. */
#define LOG_RETRY_DELAY_MS      5U

/** Size of the shared log line buffer (stack-allocated, not heap). */
#define LOG_BUF_SIZE            128U

/* ===========================================================================
 * Private helpers
 * ========================================================================= */

/**
 * @brief  Transmit a null-terminated string over USB CDC.
 *
 * Retries up to LOG_RETRY_MAX times if the device signals USBD_BUSY.
 * Silently drops the message after exhausting retries (no assert, because
 * a missing log line is non-fatal).
 *
 * @param  msg  Null-terminated ASCII string to send.
 */
static void log_usb(const char *msg)
{
    uint16_t len = (uint16_t)strlen(msg);
    if (len == 0U)
    {
        return;
    }

    for (uint8_t attempt = 0U; attempt < LOG_RETRY_MAX; attempt++)
    {
        /*
         * Acquire g_cdcMutex before entering CDC_Transmit_FS().
         * ethernet_task calls eth_cdc_log() (same mutex) on debug log lines,
         * so without this guard two tasks can corrupt the USB CDC internal
         * TxBuffer/TxLength fields simultaneously.
         * Timeout 50 ms >> one USB FS bulk transfer (~1 ms) but short enough
         * to not stall this task noticeably.
         */
        if (osMutexAcquire(g_cdcMutex, 50U) != osOK)
        {
            osDelay(LOG_RETRY_DELAY_MS);
            continue;
        }
        /*
         * CDC_Transmit_FS takes a non-const pointer but does not modify the
         * buffer; the cast is safe here.
         */
        uint8_t result = CDC_Transmit_FS((uint8_t *)msg, len);
        osMutexRelease(g_cdcMutex);
        if (result == USBD_OK)
        {
            return;
        }
        osDelay(LOG_RETRY_DELAY_MS);
    }
    /* Retries exhausted — drop silently. */
}

/**
 * @brief  Format an 8-bit value as a zero-padded 8-digit binary string.
 *
 * @param  val   Value to format.
 * @param  buf   Output buffer, must be at least 11 bytes ("0b" + 8 digits + NUL).
 */
static void fmt_binary8(uint8_t val, char *buf)
{
    buf[0] = '0';
    buf[1] = 'b';
    for (int8_t i = 7; i >= 0; i--)
    {
        buf[2 + (7 - i)] = (char)('0' + ((val >> i) & 0x01U));
    }
    buf[10] = '\0';
}

/**
 * @brief  Log the assigned IP address, subnet mask, gateway, and web URL.
 */
static void log_ip_event(void)
{
    uint32_t ip, mask, gw;

    if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
    {
        ip   = g_appData.ip_addr;
        mask = g_appData.ip_mask;
        gw   = g_appData.ip_gw;
        osMutexRelease(g_dataMutex);
    }
    else
    {
        log_usb("[NET] ERR: mutex timeout\r\n");
        return;
    }

    /* LwIP stores addresses in network byte order (big-endian).
     * The STM32 is little-endian, so byte 0 is the LSB of the uint32.
     * Extract octets accordingly: octet A = ip & 0xFF, then B, C, D. */
    char line[LOG_BUF_SIZE];

    snprintf(line, sizeof(line), "[NET] IP   : %lu.%lu.%lu.%lu\r\n",
             (unsigned long)(ip        & 0xFFUL),
             (unsigned long)((ip >> 8) & 0xFFUL),
             (unsigned long)((ip >> 16) & 0xFFUL),
             (unsigned long)((ip >> 24) & 0xFFUL));
    log_usb(line);

    snprintf(line, sizeof(line), "[NET] Mask : %lu.%lu.%lu.%lu\r\n",
             (unsigned long)(mask        & 0xFFUL),
             (unsigned long)((mask >> 8) & 0xFFUL),
             (unsigned long)((mask >> 16) & 0xFFUL),
             (unsigned long)((mask >> 24) & 0xFFUL));
    log_usb(line);

    snprintf(line, sizeof(line), "[NET] GW   : %lu.%lu.%lu.%lu\r\n",
             (unsigned long)(gw        & 0xFFUL),
             (unsigned long)((gw >> 8) & 0xFFUL),
             (unsigned long)((gw >> 16) & 0xFFUL),
             (unsigned long)((gw >> 24) & 0xFFUL));
    log_usb(line);

    snprintf(line, sizeof(line), "[NET] Web  : http://%lu.%lu.%lu.%lu\r\n",
             (unsigned long)(ip        & 0xFFUL),
             (unsigned long)((ip >> 8) & 0xFFUL),
             (unsigned long)((ip >> 16) & 0xFFUL),
             (unsigned long)((ip >> 24) & 0xFFUL));
    log_usb(line);

    extern volatile uint32_t g_eth_tx_count;
    extern volatile uint32_t g_eth_rx_count;
    snprintf(line, sizeof(line), "[NET] TX   : %lu  RX: %lu\r\n",
             (unsigned long)g_eth_tx_count,
             (unsigned long)g_eth_rx_count);
    log_usb(line);
}

/**
 * @brief  Return a short ASCII label for a Modbus error code.
 */
static const char *mb_err_str(MbErrCode_t code)
{
    switch (code)
    {
        case MB_ERR_NONE:        return "NONE";
        case MB_ERR_HAL_RX:      return "HAL_RX_FAIL";
        case MB_ERR_HAL_TX:      return "HAL_TX_FAIL";
        case MB_ERR_TIMEOUT:     return "TIMEOUT";
        case MB_ERR_SHORT_FRAME: return "SHORT_FRAME";
        case MB_ERR_BAD_ADDR:    return "BAD_ADDR";
        case MB_ERR_BAD_FC:      return "BAD_FC";
        case MB_ERR_CRC:         return "CRC_MISMATCH";
        default:                 return "UNKNOWN";
    }
}

/**
 * @brief  Log Modbus error event: total error count and per-slave last fault.
 *
 * Called when EVT_MODBUS_ERR is set by modbus_task after any failed poll.
 * Format:
 *   [MB]  ERR_TOTAL:5  S1:TIMEOUT  S2:NONE
 */
static void log_modbus_err_event(void)
{
    uint32_t    err_cnt;
    MbErrCode_t last_err[MB_SLAVE_COUNT];

    if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
    {
        err_cnt = g_appData.mb_err_count;
        for (uint8_t i = 0U; i < MB_SLAVE_COUNT; i++)
        {
            last_err[i] = g_appData.mb_slave[i].mb_last_err;
        }
        osMutexRelease(g_dataMutex);
    }
    else
    {
        log_usb("[MB]  ERR: mutex timeout\r\n");
        return;
    }

    char line[LOG_BUF_SIZE];
    snprintf(line, sizeof(line), "[MB]  ERR_TOTAL:%lu  S1:%s  S2:%s\r\n",
             (unsigned long)err_cnt,
             mb_err_str(last_err[0]),
             mb_err_str(last_err[1]));
    log_usb(line);
}

/**
 * @brief  Log Modbus poll counters and holding registers for both slaves.
 */
static void log_modbus_event(void)
{
    uint32_t rx_cnt, err_cnt;
    uint16_t s1_reg[2], s2_reg[2];
    uint8_t  s1_conn, s2_conn;

    if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
    {
        rx_cnt   = g_appData.mb_rx_count;
        err_cnt  = g_appData.mb_err_count;
        s1_reg[0] = g_appData.mb_slave[0].holding_reg[0];
        s1_reg[1] = g_appData.mb_slave[0].holding_reg[1];
        s1_conn   = g_appData.mb_slave[0].connected;
        s2_reg[0] = g_appData.mb_slave[1].holding_reg[0];
        s2_reg[1] = g_appData.mb_slave[1].holding_reg[1];
        s2_conn   = g_appData.mb_slave[1].connected;
        osMutexRelease(g_dataMutex);
    }
    else
    {
        log_usb("[MB]  ERR: mutex timeout\r\n");
        return;
    }

    char line[LOG_BUF_SIZE];

    snprintf(line, sizeof(line),
             "[MB]  RX:%lu  ERR:%lu\r\n",
             (unsigned long)rx_cnt,
             (unsigned long)err_cnt);
    log_usb(line);

    snprintf(line, sizeof(line),
             "[MB]  S1(%s) HR[0]=0x%04X HR[1]=0x%04X\r\n",
             s1_conn ? "OK" : "--",
             (unsigned int)s1_reg[0],
             (unsigned int)s1_reg[1]);
    log_usb(line);

    snprintf(line, sizeof(line),
             "[MB]  S2(%s) HR[0]=0x%04X HR[1]=0x%04X\r\n",
             s2_conn ? "OK" : "--",
             (unsigned int)s2_reg[0],
             (unsigned int)s2_reg[1]);
    log_usb(line);
}

/**
 * @brief  Log digital input state and relay output state in binary notation.
 */
static void log_io_event(void)
{
    uint8_t di, relay;

    if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
    {
        di    = g_appData.di_state;
        relay = g_appData.relay_state;
        osMutexRelease(g_dataMutex);
    }
    else
    {
        log_usb("[IO]  ERR: mutex timeout\r\n");
        return;
    }

    char di_str[11];
    char relay_str[11];
    fmt_binary8(di, di_str);
    fmt_binary8(relay, relay_str);

    char line[LOG_BUF_SIZE];
    snprintf(line, sizeof(line),
             "[IO]  DI   : %s  Relay: %s\r\n",
             di_str, relay_str);
    log_usb(line);
}

/* ===========================================================================
 * Public API
 * ========================================================================= */

void USB_Task_Init(void)
{
    /* No pre-scheduler resources needed. */
}

void USB_Task_Run(void *argument)
{
    (void)argument;

    /* -----------------------------------------------------------------------
     * 1. USB device stack already initialised in main() before osKernelStart().
     *    Wait for host enumeration to complete before sending the boot banner.
     * ---------------------------------------------------------------------- */
#ifndef DEBUG
    HAL_IWDG_Refresh(&hiwdg);   /* kick before blocking — IWDG timeout is 4 s */
#endif
    osDelay(USB_ENUM_DELAY_MS);

    /* -----------------------------------------------------------------------
     * 2. Boot banner.
     * ---------------------------------------------------------------------- */
    log_usb("=== STM32 Gateway Boot ===\r\n");

    /* Log initial IO state (relay and DI). */
    log_io_event();

    /* -----------------------------------------------------------------------
     * 3. Event-driven log loop.
     * ---------------------------------------------------------------------- */
    for (;;)
    {
        /* Kick the IWDG every iteration (≤ 1000 ms).
         * USB_Task runs at osPriorityBelowNormal — the lowest in the system.
         * If any higher-priority task deadlocks and starves this task for
         * > 2 s, the watchdog fires and resets the MCU. */
    #ifndef DEBUG
        HAL_IWDG_Refresh(&hiwdg);
    #endif

        /*
         * Block until at least one event bit is set OR 1000 ms elapses.
         * pdTRUE  = clear ALL waited bits on exit (each consumer clears its own).
         * pdFALSE = wake on ANY bit (not requiring all bits simultaneously).
         */
        EventBits_t bits = xEventGroupWaitBits(
            g_systemEvents,
            EVT_ALL_TASKS,
            pdTRUE,                    /* clear matched bits on exit */
            pdFALSE,                   /* wake on any bit            */
            pdMS_TO_TICKS(1000U)       /* 1 s max block — keep IWDG alive */
        );

        if (bits & EVT_IP_ACQUIRED)
        {
            log_ip_event();
        }

        if (bits & EVT_MODBUS_RX)
        {
            log_modbus_event();
        }

        if (bits & EVT_MODBUS_ERR)
        {
            /* Throttle: only log once every 10 s to prevent USB CDC overflow.
             * modbus_task sets EVT_MODBUS_ERR every ~100 ms when slaves are
             * absent, which would flood CDC_Transmit_FS and cause COM port
             * resets on the host. */
            static uint32_t last_mb_log_tick = 0U;
            uint32_t now = HAL_GetTick();
            if ((now - last_mb_log_tick) >= 10000U)
            {
                last_mb_log_tick = now;
                log_modbus_err_event();
            }
        }

        if (bits & EVT_IO_CHANGED)
        {
            log_io_event();
        }

        /* Periodic IO state logging every 5 seconds. */
        {
            static uint32_t last_io_log_tick = 0U;
            uint32_t now = HAL_GetTick();
            if ((now - last_io_log_tick) >= 5000U)
            {
                last_io_log_tick = now;
                log_io_event();
            }
        }
    }
}

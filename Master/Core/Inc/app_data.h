/**
 * @file    app_data.h
 * @brief   Shared application state, inter-task event bits, and global handles
 *          for the STM32 gateway firmware.
 *
 * Architecture
 * ------------
 * A single AppData_t structure holds the runtime state produced by all tasks.
 * Access is serialised via a FreeRTOS mutex (g_dataMutex).
 * State changes are signalled via a FreeRTOS event group (g_systemEvents).
 *
 * Ownership model
 * ---------------
 *   Ethernet task : writes ip_addr / ip_mask / ip_gw
 *   Modbus task   : writes mb_slave[0..1] (holding_reg, connected), mb_rx/err_count
 *   IO task       : writes di_state, relay_state
 *   All others    : read only — acquire mutex, copy to local, release
 *
 * Adding a new task
 * -----------------
 * 1. Reserve a new event-bit range (bits 8–15 are uncommitted).
 * 2. Add fields to AppData_t below with a comment naming the owner task.
 * 3. Do NOT change existing field offsets.
 *
 * Thread-safety rules (MUST follow in every task)
 * ------------------------------------------------
 * Write : osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS)
 *         → update field(s)
 *         → osMutexRelease(g_dataMutex)
 *         → xEventGroupSetBits(g_systemEvents, EVT_xxx)   // after release
 *
 * Read  : osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS)
 *         → local_var = g_appData.field   // copy under lock
 *         → osMutexRelease(g_dataMutex)
 *         → use local_var                 // never dereference after release
 *
 * Wait  : xEventGroupWaitBits(g_systemEvents, EVT_xxx, pdTRUE, pdFALSE, timeout)
 *         pdTRUE = clear bits on exit (each task clears only the bits it waits on)
 */

#ifndef APP_DATA_H
#define APP_DATA_H

#include "cmsis_os.h"       /* osMutexId_t (CMSIS-RTOS v2)                   */
#include "FreeRTOS.h"       /* configASSERT, BaseType_t                      */
#include "event_groups.h"   /* EventGroupHandle_t, EventBits_t               */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * Compile-time constants
 * ========================================================================= */

/** Maximum wait time when acquiring g_dataMutex. */
#define MUTEX_TIMEOUT_MS    100U

/* ===========================================================================
 * Modbus layout constants
 *
 * Defined here (not in modbus_task.h) because they are part of the
 * AppData_t memory layout — both modbus_task.c and usb_task.c need them.
 * ========================================================================= */

/** Number of Modbus slaves polled by the master task. */
#define MB_SLAVE_COUNT       2U

/** Number of FC 0x03 holding registers read from each slave per poll cycle. */
#define MB_SLAVE_REG_COUNT   8U

/* ===========================================================================
 * System Event Bits
 *
 * Each task owns a reserved range so new tasks can be added without
 * touching existing code.
 *
 *   Bit  0     – Ethernet task
 *   Bit  1     – Modbus task
 *   Bit  2     – IO task
 *   Bits 3–7   – Reserved for future tasks
 *   Bits 8–23  – Available for application-level events
 * ========================================================================= */

/** Ethernet → all: IP address obtained via DHCP. */
#define EVT_IP_ACQUIRED     ((EventBits_t)(1U << 0))

/** Modbus → USB: valid RTU frame received and parsed. */
#define EVT_MODBUS_RX       ((EventBits_t)(1U << 1))

/** IO → USB / Modbus: digital-input state changed or relay command arrived. */
#define EVT_IO_CHANGED      ((EventBits_t)(1U << 2))

/**
 * Web server (tcpip_thread) → IO task: a relay command was written to
 * g_appData.mb_coil[] via CGI (/relay.cgi).  IO task wakes and calls
 * relay_mask_from_coils() to latch the new state onto GPIO.
 * This bit is separate from EVT_IO_CHANGED so the USB logger is not
 * spammed on every HTTP relay request.
 */
#define EVT_RELAY_CMD       ((EventBits_t)(1U << 3))

/** Modbus → USB: a poll cycle completed with an error (timeout / CRC / HAL). */
#define EVT_MODBUS_ERR      ((EventBits_t)(1U << 4))

/* Bits 5–7: reserved — do not use until a new task claims them. */

/** Convenience mask — USB task waits on these events.
 * Note: EVT_RELAY_CMD is intentionally excluded — USB logger does not
 * need to wake on relay commands issued via HTTP CGI. */
#define EVT_ALL_TASKS       (EVT_IP_ACQUIRED | EVT_MODBUS_RX | EVT_IO_CHANGED | EVT_MODBUS_ERR)

/* ===========================================================================
 * Modbus error codes
 * ========================================================================= */

/**
 * @brief  Identifies the specific failure that caused a Modbus poll to fail.
 *
 * Stored in MbSlaveData_t.mb_last_err after every failed cycle so any task
 * (USB logger, web server) can report the exact fault without extra flags.
 */
typedef enum
{
    MB_ERR_NONE        = 0,  /**< Last poll succeeded — no error.            */
    MB_ERR_HAL_RX      = 1,  /**< HAL_UART_Receive_DMA() returned non-OK.    */
    MB_ERR_HAL_TX      = 2,  /**< HAL_UART_Transmit() returned non-OK.       */
    MB_ERR_TIMEOUT     = 3,  /**< No response within MB_RESPONSE_TIMEOUT_MS. */
    MB_ERR_SHORT_FRAME = 4,  /**< Response shorter than expected (MB_RESPONSE_LEN). */
    MB_ERR_BAD_ADDR    = 5,  /**< Address byte in response ≠ slave address.  */
    MB_ERR_BAD_FC      = 6,  /**< FC byte ≠ 0x03 (slave sent exception?).    */
    MB_ERR_CRC         = 7,  /**< CRC-16 mismatch.                           */
} MbErrCode_t;

/* ===========================================================================
 * Per-slave Modbus data
 * ========================================================================= */

/**
 * @brief  Snapshot of one Modbus slave's state.
 *
 * Written exclusively by modbus_task in a single critical section after a
 * complete, CRC-validated FC 0x03 response is received from that slave.
 * All other tasks must read under g_dataMutex.
 */
typedef struct
{
    /** Latest FC 0x03 holding register values (registers 0 … MB_SLAVE_REG_COUNT-1). */
    uint16_t holding_reg[MB_SLAVE_REG_COUNT];

    /** 1 = slave responded within timeout; 0 = timeout or CRC error. */
    uint8_t  connected;

    /** Specific error from the most recent failed poll cycle (MB_ERR_NONE = OK). */
    MbErrCode_t mb_last_err;
} MbSlaveData_t;

/* ===========================================================================
 * Shared Application Data
 * ========================================================================= */

/**
 * @brief  Complete runtime state of the gateway.
 *
 * All fields are zero-initialised at startup by App_Data_Init().
 * See the ownership model in the file header for write/read rules.
 */
typedef struct
{
    /* -------------------------------------------------------------------
     * Ethernet — owner: ethernet_task
     * ----------------------------------------------------------------- */

    /** IPv4 address in network byte order (0 = not yet assigned). */
    uint32_t ip_addr;

    /** Subnet mask in network byte order. */
    uint32_t ip_mask;

    /** Default gateway in network byte order. */
    uint32_t ip_gw;

    /* -------------------------------------------------------------------
     * Modbus RTU master — owner: modbus_task  (others: read-only)
     *
     * The master polls MB_SLAVE_COUNT slaves sequentially.
     * mb_slave[0] → slave address 1
     * mb_slave[1] → slave address 2
     * ----------------------------------------------------------------- */

    /** Per-slave register data and connectivity status. */
    MbSlaveData_t mb_slave[MB_SLAVE_COUNT];

    /** Relay coil commands (written by web server / external source).
     *  IO task reads these to drive GPIO relay outputs.
     *  0 = OFF, 1 = ON.  Not connected to Modbus coil protocol. */
    uint8_t  mb_coil[8];

    /** Total number of valid FC 0x03 responses received across all slaves. */
    uint32_t mb_rx_count;

    /** Total number of timeouts and CRC errors across all slaves. */
    uint32_t mb_err_count;

    /* -------------------------------------------------------------------
     * Digital IO — owner: io_task  (others: read-only)
     * ----------------------------------------------------------------- */

    /**
     * Debounced state of PB4–PB7.
     * Bit layout: bit0=PB4, bit1=PB5, bit2=PB6, bit3=PB7.
     * Logic: 1 = input active (pulled LOW externally), 0 = inactive.
     */
    uint8_t di_state;

    /**
     * Current relay output state (mirrors GPIO).
     * Bit layout: bit0=Relay0 … bit3=Relay3.  1 = energised.
     */
    uint8_t relay_state;

    /* -------------------------------------------------------------------
     * Future tasks — add new fields here; do not reorder the fields above
     * ----------------------------------------------------------------- */

} AppData_t;

/* ===========================================================================
 * Global object declarations  (defined in app_data.c)
 * ========================================================================= */

/** Single shared state object.  Access must be guarded by g_dataMutex. */
extern AppData_t          g_appData;

/**
 * Recursive priority-inheriting mutex that serialises access to g_appData.
 * Created by App_Data_Init() before the scheduler starts.
 */
extern osMutexId_t        g_dataMutex;

/**
 * Non-recursive mutex that serialises all CDC_Transmit_FS() calls.
 * Shared between ethernet_task and usb_task to prevent concurrent access
 * to the USB CDC TX path, which is not thread-safe internally.
 * Created by App_Data_Init() before the scheduler starts.
 */
extern osMutexId_t        g_cdcMutex;

/**
 * Event group for inter-task notifications.
 * Created by App_Data_Init() before the scheduler starts.
 */
extern EventGroupHandle_t g_systemEvents;

/* ===========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief  Initialise shared state, mutex, and event group.
 *
 * Must be called exactly once, before osKernelStart(), typically from
 * App_Data_Init() inside freertos.c or from main() after HAL init.
 * Asserts on allocation failure.
 */
void App_Data_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_DATA_H */

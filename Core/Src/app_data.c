/**
 * @file    app_data.c
 * @brief   Definitions of the gateway's global shared state objects.
 *
 * Architecture
 * ------------
 * This file owns the three objects that form the backbone of inter-task
 * communication:
 *
 *   g_appData      – Plain C struct holding all runtime state.  Protected by
 *                    the mutex below; no ISR access permitted.
 *
 *   g_dataMutex    – CMSIS-RTOS v2 recursive, priority-inheriting mutex.
 *                    Recursive to allow the same task to re-acquire without
 *                    deadlocking.  Priority inheritance prevents inversion.
 *
 *   g_systemEvents – FreeRTOS EventGroup with one bit per task notification
 *                    (see EVT_xxx in app_data.h).  Bits are set after
 *                    releasing the mutex; consumers clear on receipt.
 *
 * Initialisation order
 * --------------------
 * Call App_Data_Init() from freertos.c / MX_FREERTOS_Init() BEFORE
 * osKernelStart(), so every task sees valid handles from its first line.
 */

#include "app_data.h"

/* ===========================================================================
 * Global definitions
 * ========================================================================= */

/** All fields zero-initialised by C zero-init rules (ISO C99 §6.7.9). */
AppData_t          g_appData      = { 0 };

/** Created in App_Data_Init(); NULL until then. */
osMutexId_t        g_dataMutex    = NULL;

/** Created in App_Data_Init(); NULL until then. */
osMutexId_t        g_cdcMutex     = NULL;

/** Created in App_Data_Init(); NULL until then. */
EventGroupHandle_t g_systemEvents = NULL;

/* ===========================================================================
 * Public API
 * ========================================================================= */

void App_Data_Init(void)
{
    /*
     * Recursive  : allows the owning task to re-acquire without self-deadlock.
     * PrioInherit: the mutex holder is temporarily boosted to the highest
     *              priority of any task waiting on the mutex, preventing
     *              priority inversion on a mixed-priority system.
     */
    static const osMutexAttr_t s_mutex_attr = {
        .name      = "AppDataMutex",
        .attr_bits = osMutexRecursive | osMutexPrioInherit,
        .cb_mem    = NULL,
        .cb_size   = 0U,
    };

    g_dataMutex = osMutexNew(&s_mutex_attr);
    configASSERT(g_dataMutex != NULL);

    /* Non-recursive mutex for CDC_Transmit_FS() serialisation.  Priority
     * inheritance prevents usb_task (Low) from blocking ethernet_task
     * (Normal) for longer than a single USB bulk transfer (~1 ms). */
    static const osMutexAttr_t s_cdc_mutex_attr = {
        .name      = "CdcMutex",
        .attr_bits = osMutexPrioInherit,
        .cb_mem    = NULL,
        .cb_size   = 0U,
    };
    g_cdcMutex = osMutexNew(&s_cdc_mutex_attr);
    configASSERT(g_cdcMutex != NULL);

    /*
     * xEventGroupCreate() allocates from the FreeRTOS heap.  The assertion
     * will trip at startup (before the scheduler runs) if heap is exhausted,
     * which is the safest possible failure point.
     */
    g_systemEvents = xEventGroupCreate();
    configASSERT(g_systemEvents != NULL);
}

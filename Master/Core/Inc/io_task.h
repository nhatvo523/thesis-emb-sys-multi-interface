/**
 * @file    io_task.h
 * @brief   Public interface for the IO task (digital inputs + relay outputs).
 *
 * The IO task reads PB4–PB7 with software debounce and mirrors the relay
 * command from g_appData.relay_state (written by Modbus task via mb_coil[])
 * to the GPIO relay output pins.
 *
 * Only IO_Task_Init() and IO_Task_Run() are exposed here; all internal
 * state is file-scoped inside io_task.c.
 */

#ifndef IO_TASK_H
#define IO_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief  Called once from freertos.c before osKernelStart() to perform any
 *         static initialisation needed by the IO task (currently a no-op;
 *         provided for symmetry with other tasks).
 */
void IO_Task_Init(void);

/**
 * @brief  IO task entry point.
 *
 * Signature is compatible with osThreadFunc_t / TaskFunction_t.
 * Pass NULL as @p argument; the parameter is unused.
 *
 * @param  argument  Unused (pass NULL).
 */
void IO_Task_Run(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* IO_TASK_H */

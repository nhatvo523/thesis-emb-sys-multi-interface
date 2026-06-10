/**
 * @file    usb_task.h
 * @brief   Public interface for the USB CDC log task.
 *
 * The USB task waits on g_systemEvents for EVT_IP_ACQUIRED, EVT_MODBUS_RX,
 * and EVT_IO_CHANGED, then formats and transmits a log line over the USB
 * CDC virtual COM port.
 *
 * Only USB_Task_Init() and USB_Task_Run() are exposed here.
 */

#ifndef USB_TASK_H
#define USB_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Called once before osKernelStart() for any pre-scheduler setup.
 *         Currently a no-op; provided for symmetry with other tasks.
 */
void USB_Task_Init(void);

/**
 * @brief  USB CDC log task entry point (osThreadFunc_t / TaskFunction_t).
 *
 * Execution sequence:
 *   1. MX_USB_DEVICE_Init() — enumerate the CDC device.
 *   2. osDelay(2000)        — wait for host to recognise the port.
 *   3. Transmit boot banner.
 *   4. Loop: xEventGroupWaitBits on EVT_IP_ACQUIRED | EVT_MODBUS_RX |
 *            EVT_IO_CHANGED (blocking, auto-clear bits on exit).
 *      - EVT_IP_ACQUIRED → log IP, mask, GW, and web URL.
 *      - EVT_MODBUS_RX   → log frame counters and holding registers 0–3.
 *      - EVT_IO_CHANGED  → log DI state (binary) and relay state (binary).
 *
 * @param  argument  Unused (pass NULL).
 */
void USB_Task_Run(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* USB_TASK_H */

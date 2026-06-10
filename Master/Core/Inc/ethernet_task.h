/**
 * @file    ethernet_task.h
 * @brief   Public interface for the Ethernet task (LwIP + DHCP + Web Server).
 *
 * The Ethernet task owns the LwIP stack initialisation, DHCP negotiation,
 * and link-state monitoring.  It updates g_appData with the assigned IP
 * address and signals EVT_IP_ACQUIRED to all other tasks once the address
 * is available.
 *
 * Only Ethernet_Task_Init() and Ethernet_Task_Run() are exposed here.
 */

#ifndef ETHERNET_TASK_H
#define ETHERNET_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Called once before osKernelStart() for any pre-scheduler setup.
 *         Currently a no-op; provided for symmetry with other tasks.
 */
void Ethernet_Task_Init(void);

/**
 * @brief  Ethernet task entry point (osThreadFunc_t / TaskFunction_t).
 *
 * Execution sequence:
 *   1. MX_LWIP_Init()   — starts tcpip_thread + ethernet_link_thread,
 *                         registers gnetif, begins DHCP negotiation.
 *   2. WebServer_Init() — opens TCP port 80, registers SSI/CGI handlers.
 *   3. Loop (500 ms)    — monitors link state and DHCP IP assignment;
 *                         updates g_appData and fires EVT_IP_ACQUIRED once
 *                         an IP is obtained.  Clears IP fields on link down.
 *
 * @param  argument  Unused (pass NULL).
 */
void Ethernet_Task_Run(void *argument);

/**
 * @brief  Sequence the LAN8720AI PHY power-up.
 *
 * Must be called once after MX_GPIO_Init() and before osKernelStart() so
 * that HAL_Delay() (SysTick-based) works correctly.
 *
 * Sequence:
 *   Step 1  PA8  SET   — enable ETH_VDD rail (Q401/Q400 MOSFET pair)
 *   Step 2  10 ms      — ETH_VDD bulk capacitors stabilise
 *   Step 3  PB14 SET   — start 50 MHz CMOS oscillator (Y400)
 *   Step 4  5 ms       — oscillator lock time
 *   Step 5  PB15 RESET — assert LAN8720 reset (active-low)
 *   Step 6  10 ms      — hold reset (spec min 100 µs; 10 ms margin)
 *   Step 7  PB15 SET   — release LAN8720 reset
 *   Step 8  100 ms     — LAN8720 internal boot; MDIO registers become accessible
 *   Step 9  (automatic) MX_LWIP_Init() → HAL_ETH_Init() called by
 *           Ethernet_Task_Run() after the RTOS scheduler starts
 */
void ETH_PowerUp(void);

/**
 * @brief  Safely power-down the LAN8720AI PHY.
 *
 * Sequence:
 *   Step 1  PB15 RESET — assert PHY reset before cutting supply
 *   Step 2  1 ms
 *   Step 3  PB14 RESET — stop 50 MHz oscillator
 *   Step 4  1 ms
 *   Step 5  PA8  RESET — cut ETH_VDD (R403 pulls RST to 0 V automatically)
 */
void ETH_PowerDown(void);

/**
 * @brief  Safe system reset — shuts down the LAN8720 PHY cleanly before
 *         triggering NVIC_SystemReset().
 *
 * Use this instead of NVIC_SystemReset() everywhere in application code.
 * Calling NVIC_SystemReset() directly while ETH_VDD is live risks leaving
 * Y400 driving XTAL1 with no Vcc on the PHY (see ETH_PowerDown() rationale).
 *
 * Total execution time: ~2 ms (DWT busy-delay in ETH_PowerDown()) then reset.
 * Safe to call from any context — task, ISR, or fault handler.
 */
void System_SafeReset(void);

#ifdef __cplusplus
}
#endif

#endif /* ETHERNET_TASK_H */

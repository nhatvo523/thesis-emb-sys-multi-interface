/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ethernet_task.h"   /* ETH_PowerDown(), System_SafeReset() */
#include "FreeRTOSConfig.h"  /* configTOTAL_HEAP_SIZE for ucHeap declaration */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* Place FreeRTOS heap in CCMRAM (0x10000000, 64KB, CPU-only, zero-wait-state).
 * DMA controllers cannot access CCMRAM, but FreeRTOS heap only holds task
 * stacks, TCBs, queues, mutexes and event groups - none of which are DMA targets. */
uint8_t ucHeap[configTOTAL_HEAP_SIZE] __attribute__((section(".ccmram")));
/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
    /* configCHECK_FOR_STACK_OVERFLOW = 2: pattern check on every context
     * switch catches overflow early, before it corrupts enough RAM to
     * trigger a HardFault with no traceable cause.
     *
     * ETH_PowerDown() uses DWT busy-delay — safe here because this hook
     * is called from the scheduler (PendSV), not from an ISR with
     * restrictions on FreeRTOS API use, and does not call any RTOS API.
     *
     * After PowerDown, spin with interrupts disabled so the watchdog
     * (if enabled) eventually kicks in, or the developer can attach a
     * debugger and inspect pcTaskName to identify the offending task. */
    (void)xTask;
  #ifndef DEBUG
    ETH_PowerDown();
  #endif
    taskDISABLE_INTERRUPTS();
    for (;;);
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
    /* Heap exhaustion — pvPortMalloc() returned NULL.
     * This is fatal: a task, queue, mutex, or event group could not be
     * created. The system cannot run correctly from this point.
     *
     * Same shutdown sequence as the stack overflow hook: power down PHY,
     * then halt. Increase configTOTAL_HEAP_SIZE in FreeRTOSConfig.h if
     * this fires during normal initialisation. */
  #ifndef DEBUG
    ETH_PowerDown();
  #endif
    taskDISABLE_INTERRUPTS();
    for (;;);
}
/* USER CODE END 5 */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */


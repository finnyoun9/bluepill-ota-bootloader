/**
 * @file    FreeRTOSConfig.h
 * @brief   FreeRTOS configuration for STM32F103C8T6 (Cortex-M3, 20KB SRAM).
 *
 * Tailored for this OTA application: moderate heap, stream buffers enabled,
 * task notifications enabled, runtime stats optional in debug.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "stm32f1xx_hal.h"

/*---------------------------------------------------------------------------
 * Scheduler
 *---------------------------------------------------------------------------*/

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      1000U   /* 1 ms tick */
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                128U
#define configTOTAL_HEAP_SIZE                   ((size_t)(12 * 1024))  /* 12KB heap: 5 dynamic tasks need ~11KB */
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_TRACE_FACILITY                1
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configQUEUE_REGISTRY_SIZE               0

/*---------------------------------------------------------------------------
 * Memory allocation
 *---------------------------------------------------------------------------*/

#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configUSE_MALLOC_FAILED_HOOK            1
#define configAPPLICATION_ALLOCATED_HEAP        0

/*---------------------------------------------------------------------------
 * Hooks
 *---------------------------------------------------------------------------*/

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2  /* Method 2 */
#define configUSE_MALLOC_FAILED_HOOK            1

/*---------------------------------------------------------------------------
 * Runtime stats (enable for debugging)
 *---------------------------------------------------------------------------*/

#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/*---------------------------------------------------------------------------
 * Co-routines (not used)
 *---------------------------------------------------------------------------*/

#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         2

/*---------------------------------------------------------------------------
 * Stream buffers
 *---------------------------------------------------------------------------*/

#define configUSE_STREAM_BUFFERS                1
#define configSTREAM_BUFFER_TRIGGER_LEVEL_TEST_MARGIN 1

/*---------------------------------------------------------------------------
 * Software timers
 *---------------------------------------------------------------------------*/

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                8
#define configTIMER_TASK_STACK_DEPTH            256

/*---------------------------------------------------------------------------
 * Optional features
 *---------------------------------------------------------------------------*/

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xEventGroupSetBitFromISR        1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 0
#define INCLUDE_xTaskGetHandle                  0

/*---------------------------------------------------------------------------
 * Cortex-M3 specific
 *---------------------------------------------------------------------------*/

#define configPRIO_BITS                         4  /* STM32F1: 4 priority bits */

/* Lowest priority — used by port.c */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 0x0F

/* SysTick / PendSV priority — must be the lowest */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 0x05

#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/*---------------------------------------------------------------------------
 * Assert
 *---------------------------------------------------------------------------*/

#define configASSERT(x) if ((x) == 0) { taskDISABLE_INTERRUPTS(); while(1); }

/*---------------------------------------------------------------------------
 * FreeRTOS MPU (not used on F103)
 *---------------------------------------------------------------------------*/

#define configENABLE_MPU                        0
#define configENABLE_FPU                        0
#define configENABLE_TRUSTZONE                  0
#define configRUN_FREERTOS_SECURE_ONLY          0

#endif /* FREERTOS_CONFIG_H */

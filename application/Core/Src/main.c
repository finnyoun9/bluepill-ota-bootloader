/**
 * @file    main.c
 * @brief   Application entry point and FreeRTOS task implementations.
 *
 * Runs from 0x08002000. Sets SCB->VTOR at startup to relocate the
 * interrupt vector table. Integrates FreeRTOS with the shared protocol
 * stack to communicate with the ESP32 co-processor.
 *
 * Tasks:
 *   vCommTask      — UART frame receive/send, protocol parsing
 *   vControlTask   — OTA trigger (writes config + resets), version reporting
 *   vAppTask       — User application logic (placeholder)
 *   vLedTask       — Status LED heartbeat
 *   vMonitorTask   — System health, stack/heap monitoring
 */

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "stream_buffer.h"
#include <string.h>

#include "../../../shared/protocol.h"
#include "../../../shared/ota_config.h"
#include "app_tasks.h"
#include "uart_comm.h"
#include "cmd_handler.h"

/*---------------------------------------------------------------------------
 * Pin definitions
 *---------------------------------------------------------------------------*/

#define LED_PORT        GPIOC
#define LED_PIN         GPIO_PIN_13

/*---------------------------------------------------------------------------
 * Communication handles
 *---------------------------------------------------------------------------*/

static QueueHandle_t        g_cmd_queue;       /* ProtoFrame_t*: comm → control */
static QueueHandle_t        g_data_queue;      /* Raw bytes: comm → app */
static EventGroupHandle_t   g_event_group;
static StreamBufferHandle_t g_rx_stream;

/* The CMSIS startup file copies .data only. ota_config.c places the short
 * Flash erase/program wrappers in .ramfunc, so initialize that section before
 * the control task can write the OTA configuration page. */
extern uint8_t _siramfunc;
extern uint8_t _sramfunc;
extern uint8_t _eramfunc;

/* Event group bits */
#define EVENT_OTA_AVAILABLE  (1 << 0)
#define EVENT_CONNECTED      (1 << 1)
#define EVENT_ERROR          (1 << 2)

/*---------------------------------------------------------------------------
 * System init
 *---------------------------------------------------------------------------*/

static void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();

    /* 8MHz HSE crystal → PLL ×8 → SYSCLK 64MHz. */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL     = RCC_PLL_MUL8;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        /* Clock configuration failure → halt with LED off. */
        while (1);
    }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;   /* PCLK1 = 32MHz (max 36MHz) */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;   /* PCLK2 = 64MHz */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        while (1);
    }
}

static void ramfunc_init(void) {
    const uint8_t *src = &_siramfunc;
    uint8_t *dst = &_sramfunc;

    while (dst < &_eramfunc) {
        *dst++ = *src++;
    }
}

static void system_init(void) {
    /* VTOR relocation — MUST be first */
    SCB->VTOR = APP_BASE;

    HAL_Init();
    SystemClock_Config();

    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = LED_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gpio);
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET); /* LED off */
}

/*---------------------------------------------------------------------------
 * vCommTask — UART protocol handler
 *---------------------------------------------------------------------------*/

static void vCommTask(void *pvParameters) {
    (void)pvParameters;
    ProtoParser_t parser;
    proto_parser_init(&parser);

    uint8_t rx_byte;
    size_t rx_count;

    for (;;) {
        /* Block waiting for UART bytes from the stream buffer */
        rx_count = xStreamBufferReceive(g_rx_stream, &rx_byte, 1,
                                        pdMS_TO_TICKS(100));

        if (rx_count > 0) {
            const ProtoFrame_t *f = proto_parser_feed(&parser, rx_byte);
            if (f != NULL) {
                /* Valid frame received — dispatch */
                cmd_handler_dispatch(f);
            }
        }
    }
}

/*---------------------------------------------------------------------------
 * vControlTask — OTA trigger and system control
 *---------------------------------------------------------------------------*/

static void vControlTask(void *pvParameters) {
    (void)pvParameters;

    /* Report current firmware version on startup */
    BootConfig_t cfg;
    if (ota_config_read(&cfg)) {
        uint32_t version = cfg.fw_version;
        cmd_handler_send_frame(CMD_STATUS_RSP, (uint8_t *)&version, 4);
    }

    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(
            g_event_group,
            EVENT_OTA_AVAILABLE,
            pdTRUE,   /* Clear on exit */
            pdFALSE,  /* Wait for any (not all) */
            portMAX_DELAY
        );

        if (bits & EVENT_OTA_AVAILABLE) {
            /* A new firmware version is available on the ESP32.
             * Read the version from the event group (passed via queue). */
            uint32_t pending_version;
            if (xQueueReceive(g_cmd_queue, &pending_version, 0) == pdPASS) {
                /* Write OTA request to config then reboot into bootloader */
                if (ota_config_request_update(pending_version, 0)) {
                    /* Confirm only after the config page is valid. The ESP32
                     * waits for this before it begins the bootloader transfer. */
                    cmd_handler_send_frame(CMD_OTA_READY,
                                           (const uint8_t *)&pending_version, 4);
                    /* Small delay so the confirmation fully leaves USART1. */
                    vTaskDelay(pdMS_TO_TICKS(50));
                    NVIC_SystemReset();
                }
            }
        }
    }
}

/*---------------------------------------------------------------------------
 * vAppTask — user application logic (placeholder)
 *---------------------------------------------------------------------------*/

static void vAppTask(void *pvParameters) {
    (void)pvParameters;
    uint8_t data[64];

    for (;;) {
        /* Receive application data from ESP32 (BT serial bridge) */
        if (xQueueReceive(g_data_queue, data, pdMS_TO_TICKS(100)) == pdPASS) {
            /* TODO: Process application-specific data
             * For now, this is a placeholder for the user's logic.
             * Examples: sensor readings, relay control, data logging, etc. */
        }

        /* Application loop — user adds their logic here */

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/*---------------------------------------------------------------------------
 * vLedTask — status LED
 *---------------------------------------------------------------------------*/

static void vLedTask(void *pvParameters) {
    (void)pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(500);

    for (;;) {
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/*---------------------------------------------------------------------------
 * vMonitorTask — system health monitoring
 *---------------------------------------------------------------------------*/

static void vMonitorTask(void *pvParameters) {
    (void)pvParameters;

    for (;;) {
        /* Monitor stack high-water marks (debug builds only) */
        #if (configUSE_TRACE_FACILITY == 1)
        {
            /* Log or check task stack usage */
            /* UBaseType_t stack_free = uxTaskGetStackHighWaterMark(NULL); */
        }
        #endif

        /* Monitor heap */
        size_t free_heap = xPortGetFreeHeapSize();
        (void)free_heap; /* Can be logged via debug UART */

        /* IWDG refresh — if watchdog is enabled */
        /* HAL_IWDG_Refresh(&hiwdg); */

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/*---------------------------------------------------------------------------
 * app_tasks_init — create all tasks and primitives
 *---------------------------------------------------------------------------*/

void app_tasks_init(void) {
    /* Create communication primitives */
    g_cmd_queue   = xQueueCreate(QUEUE_CMD_LENGTH, sizeof(uint32_t));
    g_data_queue  = xQueueCreate(QUEUE_DATA_LENGTH, 64);
    g_event_group = xEventGroupCreate();
    g_rx_stream   = uart_comm_get_rx_stream();

    configASSERT(g_cmd_queue != NULL);
    configASSERT(g_data_queue != NULL);
    configASSERT(g_event_group != NULL);
    configASSERT(g_rx_stream != NULL);

    /* Create tasks */
    BaseType_t ret;

    ret = xTaskCreate(vCommTask, "Comm", STACK_COMM_TASK, NULL,
                      PRIO_COMM_TASK, NULL);
    configASSERT(ret == pdPASS);

    ret = xTaskCreate(vControlTask, "Control", STACK_CONTROL_TASK, NULL,
                      PRIO_CONTROL_TASK, NULL);
    configASSERT(ret == pdPASS);

    ret = xTaskCreate(vAppTask, "App", STACK_APP_TASK, NULL,
                      PRIO_APP_TASK, NULL);
    configASSERT(ret == pdPASS);

    ret = xTaskCreate(vLedTask, "Led", STACK_LED_TASK, NULL,
                      PRIO_LED_TASK, NULL);
    configASSERT(ret == pdPASS);

    ret = xTaskCreate(vMonitorTask, "Monitor", STACK_MONITOR_TASK, NULL,
                      PRIO_MONITOR_TASK, NULL);
    configASSERT(ret == pdPASS);
}

/*---------------------------------------------------------------------------
 * cmd_handler_dispatch — process received frames
 *---------------------------------------------------------------------------*/

void cmd_handler_dispatch(const ProtoFrame_t *f) {
    switch (f->cmd) {
    case CMD_OTA_AVAILABLE:
        if (f->len >= 4) {
            uint32_t version;
            memcpy(&version, f->payload, 4);
            xQueueSend(g_cmd_queue, &version, 0);
            xEventGroupSetBits(g_event_group, EVENT_OTA_AVAILABLE);
        }
        break;

    case CMD_APP_MSG:
        if (f->len > 0 && f->len <= 64) {
            xQueueSend(g_data_queue, f->payload, 0);
        }
        break;

    case CMD_GET_STATUS:
        {
            BootConfig_t cfg;
            uint32_t version = 0;
            if (ota_config_read(&cfg)) {
                version = cfg.fw_version;
            }
            cmd_handler_send_frame(CMD_STATUS_RSP, (uint8_t *)&version, 4);
        }
        break;

    case CMD_RESET:
        NVIC_SystemReset();
        break;

    default:
        /* Unknown command — ignore in application context */
        break;
    }
}

void cmd_handler_send_frame(uint8_t cmd, const uint8_t *payload, uint16_t len) {
    uint8_t buf[PROTO_MAX_FRAME];
    uint16_t total = proto_build_frame(buf, sizeof(buf), cmd, payload, len);
    if (total > 0) {
        /* Send over UART in task context (blocking, but small at 9600 baud). */
        uart_comm_send(buf, total);
    }
}

/*---------------------------------------------------------------------------
 * main entry point
 *---------------------------------------------------------------------------*/

int main(void) {
    ramfunc_init();

    /* VTOR already set in system_init, but set it here too defensively */
    SCB->VTOR = APP_BASE;

    system_init();

    /* Initialize USART1 (PA9/PA10) for ESP32 communication. */
    uart_comm_init(9600U);

    /* Create FreeRTOS primitives and tasks */
    app_tasks_init();

    /* Start the scheduler — does not return */
    vTaskStartScheduler();

    /* Should never reach here */
    while (1);
    return 0;
}

/*---------------------------------------------------------------------------
 * FreeRTOS hooks
 *---------------------------------------------------------------------------*/

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    (void)pcTaskName;
    /* Halt with LED on solid */
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    __disable_irq();
    while (1);
}

void vApplicationMallocFailedHook(void) {
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    __disable_irq();
    while (1);
}

/*---------------------------------------------------------------------------
 * FreeRTOS static allocation support (configSUPPORT_STATIC_ALLOCATION = 1)
 * The idle and timer tasks are created by the kernel; with static
 * allocation enabled it needs static TCB/stack buffers for them.
 *---------------------------------------------------------------------------*/

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize) {
    static StaticTask_t xIdleTaskTCB;
    static StackType_t  uxIdleTaskStack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize) {
    static StaticTask_t xTimerTaskTCB;
    static StackType_t  uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

    *ppxTimerTaskTCBBuffer   = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}

/*---------------------------------------------------------------------------
 * FreeRTOS interrupt handler aliases.
 * The STM32 startup vector table references SVC_Handler / PendSV_Handler,
 * but the FreeRTOS ARM_CM3 port implements them as vPortSVCHandler /
 * xPortPendSVHandler. Alias them so the vector table links correctly.
 * SysTick_Handler is defined above (calls xPortSysTickHandler).
 *---------------------------------------------------------------------------*/

void vPortSVCHandler(void);
void xPortPendSVHandler(void);
extern void xPortSysTickHandler(void);

/* Thin wrappers — same pattern as CubeMX-generated stm32f1xx_it.c */
void SVC_Handler(void)    { vPortSVCHandler(); }
void PendSV_Handler(void) { xPortPendSVHandler(); }

/*---------------------------------------------------------------------------
 * SysTick handler — FreeRTOS tick
 *---------------------------------------------------------------------------*/

void SysTick_Handler(void) {
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

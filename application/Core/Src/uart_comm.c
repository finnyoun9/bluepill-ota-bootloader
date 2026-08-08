/**
 * @file    uart_comm.c
 * @brief   UART communication driver implementation.
 *
 * USART1 (PA9=TX → ESP32 RX, PA10=RX ← ESP32 TX).
 * RX is interrupt-driven, feeding a FreeRTOS stream buffer.
 * TX is blocking (acceptable for the data rates involved).
 *
 * This driver is for the application only. The bootloader uses
 * a simpler bare-metal UART directly.
 */

#include "uart_comm.h"
#include "stm32f1xx_hal.h"

/*---------------------------------------------------------------------------
 * Static data
 *---------------------------------------------------------------------------*/

static UART_HandleTypeDef    g_huart2;
static StreamBufferHandle_t  g_rx_stream;
static uint8_t               g_rx_stream_buf[UART_RX_BUF_SIZE + 1];
static StaticStreamBuffer_t  g_rx_stream_struct;

/*---------------------------------------------------------------------------
 * Initialization
 *---------------------------------------------------------------------------*/

void uart_comm_init(uint32_t baud_rate) {
    /* Create stream buffer for ISR → task handoff */
    g_rx_stream = xStreamBufferCreateStatic(
        UART_RX_BUF_SIZE,
        1,                          /* Trigger level: 1 byte */
        g_rx_stream_buf,
        &g_rx_stream_struct
    );

    /* Configure USART1 */
    g_huart2.Instance          = USART1;
    g_huart2.Init.BaudRate     = baud_rate;
    g_huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    g_huart2.Init.StopBits     = UART_STOPBITS_1;
    g_huart2.Init.Parity       = UART_PARITY_NONE;
    g_huart2.Init.Mode         = UART_MODE_TX_RX;
    g_huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    g_huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_huart2);

    /* This ISR calls xStreamBufferSendFromISR(), so its numerical priority
     * must be at or below the FreeRTOS syscall threshold. */
    HAL_NVIC_SetPriority(USART1_IRQn,
                         configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    __HAL_UART_ENABLE_IT(&g_huart2, UART_IT_RXNE);
}

StreamBufferHandle_t uart_comm_get_rx_stream(void) {
    return g_rx_stream;
}

/*---------------------------------------------------------------------------
 * TX (blocking)
 *---------------------------------------------------------------------------*/

void uart_comm_send_byte(uint8_t byte) {
    while (!(USART1->SR & USART_SR_TXE));
    USART1->DR = byte;
}

void uart_comm_send(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        uart_comm_send_byte(data[i]);
    }
}

/*---------------------------------------------------------------------------
 * USART1 interrupt handler
 *---------------------------------------------------------------------------*/

void USART1_IRQHandler(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* RXNE: byte received */
    if (USART1->SR & USART_SR_RXNE) {
        uint8_t byte = (uint8_t)(USART1->DR & 0xFF);

        /* Send to stream buffer — wakes the comm task if blocked */
        (void)xStreamBufferSendFromISR(g_rx_stream, &byte, 1,
                                       &xHigherPriorityTaskWoken);
    }

    /* Overrun error */
    if (USART1->SR & USART_SR_ORE) {
        (void)USART1->DR; /* Clear ORE */
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*---------------------------------------------------------------------------
 * HAL MSP callback
 *---------------------------------------------------------------------------*/

void HAL_UART_MspInit(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_USART1_CLK_ENABLE();

        GPIO_InitTypeDef gpio = {0};

        /* PA9 = TX (Alternate Function Push-Pull) */
        gpio.Pin       = GPIO_PIN_9;
        gpio.Mode      = GPIO_MODE_AF_PP;
        gpio.Pull      = GPIO_NOPULL;
        gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &gpio);

        /* PA10 = RX (input floating) */
        gpio.Pin       = GPIO_PIN_10;
        gpio.Mode      = GPIO_MODE_INPUT;
        gpio.Pull      = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &gpio);
    }
}

/**
 * @file    uart_comm.h
 * @brief   UART communication driver for the application.
 *
 * Uses interrupt-driven RX with a FreeRTOS stream buffer to pass
 * bytes from ISR to the comm task. Protocol parsing runs in the task,
 * not the ISR.
 */

#ifndef UART_COMM_H
#define UART_COMM_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "stream_buffer.h"

/*---------------------------------------------------------------------------
 * Constants
 *---------------------------------------------------------------------------*/

#define UART_RX_BUF_SIZE        512U

/*---------------------------------------------------------------------------
 * API
 *---------------------------------------------------------------------------*/

/**
 * @brief Initialise USART1 (PA9=TX, PA10=RX) at the configured baud rate.
 *        Sets up interrupt-driven RX feeding into a stream buffer.
 */
void uart_comm_init(uint32_t baud_rate);

/**
 * @brief Get the stream buffer handle for the comm task to receive from.
 */
StreamBufferHandle_t uart_comm_get_rx_stream(void);

/**
 * @brief Send a raw byte over USART1 (blocking).
 */
void uart_comm_send_byte(uint8_t byte);

/**
 * @brief Send a buffer over USART1 (blocking).
 */
void uart_comm_send(const uint8_t *data, uint16_t len);

#endif /* UART_COMM_H */

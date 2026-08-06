/**
 * @file    bootloader.h
 * @brief   Bootloader main definitions, state machine, and app jump logic.
 */

#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>
#include <stdbool.h>

/*---------------------------------------------------------------------------
 * Bootloader states
 *---------------------------------------------------------------------------*/

typedef enum {
    ST_BOOT = 0,          /* Decide what to do on reset */
    ST_WAIT_HANDSHAKE,    /* Waiting for OTA_BEGIN from ESP32 */
    ST_OTA_ACTIVE,        /* Receiving chunks, erasing + programming flash */
    ST_OTA_VERIFY,        /* Computing CRC-32 over written image */
    ST_OTA_DONE,          /* Config updated, ready to jump */
    ST_ERROR,             /* Non-recoverable error — halt with blink code */
    ST_MAINTENANCE        /* Forced bootloader entry (BOOT0 pin) */
} BootState_t;

/*---------------------------------------------------------------------------
 * OTA context (passed through the state machine)
 *---------------------------------------------------------------------------*/

typedef struct {
    uint32_t image_size;        /* Total image size from OTA_BEGIN */
    uint32_t image_crc32;       /* Expected CRC-32 from OTA_BEGIN */
    uint32_t version;           /* Firmware version from OTA_BEGIN */
    uint32_t expected_seq;      /* Next expected chunk sequence number */
    uint32_t bytes_written;     /* Total bytes programmed so far */
} OtaContext_t;

/*---------------------------------------------------------------------------
 * Bootloader entry
 *---------------------------------------------------------------------------*/

/**
 * @brief Main bootloader loop. Called from main() after HAL init.
 *        Does not return — either jumps to app or halts.
 */
void bootloader_run(void);

/**
 * @brief Jump to the application at APP_BASE.
 *
 * Validates the application stack pointer, sets VTOR, and transfers control.
 * Does NOT return.
 */
void bootloader_jump_to_app(void) __attribute__((noreturn));

/**
 * @brief Software reset via NVIC.
 */
void bootloader_reset(void) __attribute__((noreturn));

/**
 * @brief Halt with an error blink code on the LED.
 *
 * @param code   Blink pattern identifier. Does not return.
 */
void bootloader_halt_error(uint32_t code) __attribute__((noreturn));

#endif /* BOOTLOADER_H */

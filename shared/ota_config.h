/**
 * @file    ota_config.h
 * @brief   Flash-based OTA configuration read/write utilities.
 *
 * Shared by bootloader and application. The config lives at CONFIG_BASE
 * (last pages of flash). On STM32F103, flash must be erased (sets to 0xFF)
 * before writing, and writes are halfword-at-a-time.
 */

#ifndef SHARED_OTA_CONFIG_H
#define SHARED_OTA_CONFIG_H

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
 * API
 *---------------------------------------------------------------------------*/

/**
 * @brief Read the boot configuration from flash.
 *
 * @param cfg  Pointer to caller-allocated BootConfig_t.
 * @return     true if magic and internal CRC are valid.
 */
bool ota_config_read(BootConfig_t *cfg);

/**
 * @brief Write a full boot configuration to flash.
 *
 * Erases config page(s) then programs the struct. Blocks during erase
 * (~40ms/page on F103). Call from a task, not an ISR.
 *
 * @param cfg  Valid config with correct magic and cfg_crc32 already set.
 * @return     true on success.
 */
bool ota_config_write(const BootConfig_t *cfg);

/**
 * @brief Prepare a BootConfig_t for writing: set magic and compute CRC.
 */
void ota_config_prepare(BootConfig_t *cfg);

/**
 * @brief Mark OTA request: sets boot_mode=BOOT_MODE_OTA and writes.
 *
 * Application calls this before NVIC_SystemReset().
 *
 * @param pending_version  Firmware version expected from OTA.
 * @param image_size       Expected image size in bytes.
 * @return                 true on success.
 */
bool ota_config_request_update(uint32_t pending_version, uint32_t image_size);

/**
 * @brief Mark the current firmware as valid (bootloader calls on OTA success).
 *
 * @param version     New firmware version.
 * @param image_size  Image size.
 * @param image_crc   CRC-32 of the written image.
 * @return            true on success.
 */
bool ota_config_mark_valid(uint32_t version, uint32_t image_size, uint32_t image_crc);

/**
 * @brief Mark the update as failed (bootloader calls on error).
 */
bool ota_config_mark_failed(void);

#ifdef __cplusplus
}
#endif

#endif /* SHARED_OTA_CONFIG_H */

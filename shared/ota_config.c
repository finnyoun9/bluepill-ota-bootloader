/**
 * @file    ota_config.c
 * @brief   Flash-based OTA configuration read/write.
 *
 * Uses STM32 HAL flash API. The config is stored in the last flash pages.
 * On STM32F103: page size = 1KB, write width = halfword. Page erase takes ~40ms.
 *
 * NOTE: Flash programming code must execute from RAM on STM32F1 (single bank).
 * Mark the HAL flash functions and wrappers with __attribute__((section(".ramfunc"))).
 */

/* STM32 HAL first so FLASH_BASE/FLASH_PAGE_SIZE are defined before
 * protocol.h's #ifndef guards are evaluated (include-order independent). */
#include "stm32f1xx_hal.h"
#include "ota_config.h"
#include <string.h>

/*---------------------------------------------------------------------------
 * Internal helpers — placed in RAM (.ramfunc) for F1 single-bank safety
 *---------------------------------------------------------------------------*/

/**
 * @brief Erase a single 1KB flash page.
 *
 * Must run from RAM because the CPU stalls when fetching from flash
 * during a flash erase/program operation on single-bank F1 devices.
 */
__attribute__((section(".ramfunc")))
static uint32_t flash_erase_page(uint32_t page_addr) {
    FLASH_EraseInitTypeDef erase_init = {
        .TypeErase   = FLASH_TYPEERASE_PAGES,
        .PageAddress = page_addr,
        .NbPages     = 1
    };
    uint32_t page_error = 0;
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_init, &page_error);
    if (status != HAL_OK) {
        return page_error;
    }
    return 0;
}

/**
 * @brief Program a halfword to flash. Runs from RAM.
 */
__attribute__((section(".ramfunc")))
static HAL_StatusTypeDef flash_program_halfword(uint32_t addr, uint16_t data) {
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, data);
}

/*---------------------------------------------------------------------------
 * Public API
 *---------------------------------------------------------------------------*/

bool ota_config_read(BootConfig_t *cfg) {
    if (cfg == NULL) return false;

    const uint8_t *src = (const uint8_t *)CONFIG_BASE;
    memcpy(cfg, src, sizeof(BootConfig_t));

    /* Validate magic */
    if (cfg->magic != BOOT_CONFIG_MAGIC) {
        return false;
    }

    /* Validate internal CRC */
    uint32_t computed = proto_crc32_buf((const uint8_t *)cfg,
                                         sizeof(BootConfig_t) - sizeof(uint32_t));
    if (computed != cfg->cfg_crc32) {
        return false;
    }

    return true;
}

bool ota_config_write(const BootConfig_t *cfg) {
    if (cfg == NULL || cfg->magic != BOOT_CONFIG_MAGIC) {
        return false;
    }

    HAL_FLASH_Unlock();

    /* Erase config page */
    uint32_t err = flash_erase_page(CONFIG_BASE);
    if (err != 0) {
        HAL_FLASH_Lock();
        return false;
    }

    /* Program config struct as halfwords */
    const uint16_t *src = (const uint16_t *)cfg;
    size_t halfwords = (sizeof(BootConfig_t) + 1) / 2;
    uint32_t addr = CONFIG_BASE;

    for (size_t i = 0; i < halfwords; i++) {
        uint16_t val;
        /* Handle odd byte at end */
        if ((i + 1) * 2 <= sizeof(BootConfig_t)) {
            val = src[i];
        } else {
            /* Last halfword: read existing, only overwrite first byte */
            val = 0xFF00 | ((const uint8_t *)cfg)[sizeof(BootConfig_t) - 1];
        }

        if (flash_program_halfword(addr, val) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
        addr += 2;
    }

    HAL_FLASH_Lock();

    /* Verify */
    BootConfig_t verify;
    if (!ota_config_read(&verify)) return false;
    if (memcmp(cfg, &verify, sizeof(BootConfig_t)) != 0) return false;

    return true;
}

void ota_config_prepare(BootConfig_t *cfg) {
    if (cfg == NULL) return;
    cfg->magic = BOOT_CONFIG_MAGIC;
    cfg->cfg_crc32 = proto_crc32_buf((const uint8_t *)cfg,
                                      sizeof(BootConfig_t) - sizeof(uint32_t));
}

bool ota_config_request_update(uint32_t pending_version, uint32_t image_size) {
    BootConfig_t cfg;

    /* Read existing config (preserve fw_version on first boot) */
    if (!ota_config_read(&cfg)) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.fw_version = 0;
    }

    cfg.boot_mode       = BOOT_MODE_OTA;
    cfg.pending_version = pending_version;
    cfg.image_size      = image_size;
    cfg.update_status   = UPDATE_STATUS_IN_PROGRESS;

    ota_config_prepare(&cfg);
    return ota_config_write(&cfg);
}

bool ota_config_mark_valid(uint32_t version, uint32_t image_size, uint32_t image_crc) {
    BootConfig_t cfg;

    /* Preserve pending version info, transition to valid */
    if (!ota_config_read(&cfg)) {
        memset(&cfg, 0, sizeof(cfg));
    }

    cfg.boot_mode     = BOOT_MODE_APP;
    cfg.fw_version    = version;
    cfg.image_size    = image_size;
    cfg.image_crc32   = image_crc;
    cfg.update_status = UPDATE_STATUS_OK;

    ota_config_prepare(&cfg);
    return ota_config_write(&cfg);
}

bool ota_config_mark_failed(void) {
    BootConfig_t cfg;

    if (!ota_config_read(&cfg)) {
        return false;
    }

    cfg.boot_mode     = BOOT_MODE_APP;
    cfg.update_status = UPDATE_STATUS_FAILED;

    ota_config_prepare(&cfg);
    return ota_config_write(&cfg);
}

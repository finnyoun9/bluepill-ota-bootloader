/**
 * @file    main.cpp
 * @brief   ESP32 Communication Bridge — Bluetooth SPP + WiFi OTA → STM32.
 *
 * Architecture:
 *   - BT SPP server: accepts connections from phone apps, receives firmware
 *     files and text commands.
 *   - WiFi HTTP client: downloads firmware from a URL.
 *   - UART link: communicates with STM32 bootloader/application using the
 *     shared protocol (9600 baud, framed, CRC-32).
 *   - Orchestrator: manages firmware staging (SPIFFS) and transfer state.
 *
 * Text commands over BT SPP:
 *   OTA <url>          Download firmware from URL, then transfer to STM32
 *   FW <ver>,<size>,<crc32>  Begin a Base64 Bluetooth firmware push
 *   DATA <offset>,<base64>   Append one acknowledged firmware block
 *   VERIFY             Verify the staged image size and CRC-32
 *   SEND               Transfer the staged firmware to STM32
 *   WIFI <ssid>,<pass> Configure WiFi credentials (NVS) and reconnect
 *   VERSION            Query STM32 firmware version
 *   STATUS             Show current bridge status
 *   RESET              Software reset the ESP32
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "nvs_flash.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_spp_api.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"   /* esp_bt_gap_set_device_name (IDF 6) */

#include "esp_wifi.h"
#include "esp_http_client.h"
#include "mbedtls/base64.h"

/*---------------------------------------------------------------------------
 * Shared protocol (C-compatible, included as extern "C")
 *---------------------------------------------------------------------------*/

extern "C" {
#include "../../shared/protocol.h"
}

/*---------------------------------------------------------------------------
 * Constants
 *---------------------------------------------------------------------------*/

static const char *TAG = "bridge";

/* UART to STM32 */
#define UART_STM32_NUM          UART_NUM_2
#define UART_STM32_TXD          17
#define UART_STM32_RXD          16
#define UART_STM32_RTS          UART_PIN_NO_CHANGE
#define UART_STM32_CTS          UART_PIN_NO_CHANGE
#define UART_STM32_BAUD         9600
#define UART_STM32_BUF_SIZE     2048

/* BT SPP */
#define SPP_SERVER_NAME         "STM32-OTA-Bridge"
#define SPP_TASK_STACK          8192
#define SPP_TASK_PRIO           5

/* OTA transfer */
#define OTA_CHUNK_SIZE          1024

/* SPIFFS firmware storage */
#define FW_FILE_PATH            "/spiffs/fw.bin"
#define FW_FILE_MAX_SIZE        (54 * 1024)  /* Max app size */

/* WiFi OTA download buffer */
#define HTTP_DOWNLOAD_BUF_SIZE  4096

/* WiFi credentials — override at compile time via -D WIFI_SSID/-D WIFI_PASSWORD,
 * or at runtime via the "WIFI <ssid>,<pass>" BT command (stored in NVS). */
#ifndef WIFI_SSID
#define WIFI_SSID     "YOUR_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_PASSWORD"
#endif

/*---------------------------------------------------------------------------
 * Global state
 *---------------------------------------------------------------------------*/

static uint32_t        g_spp_handle = 0;
static QueueHandle_t   g_spp_queue;       /* received SPP data (ptr + len) */

/* SPP data item carried through the queue. Keeps the real length so binary
 * firmware chunks with embedded NUL bytes survive intact. */
typedef struct {
    uint8_t *data;
    size_t   len;
} SppData_t;
static bool            g_fw_staged = false;
static bool            g_fw_receiving = false;
static uint32_t        g_fw_size = 0;
static uint32_t        g_fw_expected_size = 0;
static uint32_t        g_fw_version = 0;
static uint32_t        g_fw_crc32 = 0;
static uint32_t        g_fw_running_crc = 0;
static FILE           *g_fw_file = NULL;

/* Bluetooth SPP is a byte stream, so a terminal command can arrive in
 * several ESP_SPP_DATA_IND_EVT callbacks. Accumulate it through CR/LF. */
static char             g_cmd_buf[256];
static size_t           g_cmd_len = 0;

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/

static void uart_stm32_init(void);
static void bt_spp_init(void);
static void wifi_init_sta(void);
static void wifi_connect(const char *ssid, const char *password);
static void bt_recv_task(void *pv);
static void bt_spp_print(const char *msg);
static bool download_firmware_http(const char *url);
static bool transfer_to_stm32(void);
static bool stage_firmware_data(const uint8_t *data, size_t len);
static bool verify_staged_firmware(void);

/*---------------------------------------------------------------------------
 * UART to STM32
 *---------------------------------------------------------------------------*/

static void uart_stm32_init(void) {
    uart_config_t uart_config = {};
    uart_config.baud_rate  = UART_STM32_BAUD;
    uart_config.data_bits  = UART_DATA_8_BITS;
    uart_config.parity     = UART_PARITY_DISABLE;
    uart_config.stop_bits  = UART_STOP_BITS_1;
    uart_config.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_APB;

    ESP_ERROR_CHECK(uart_driver_install(UART_STM32_NUM,
                                         UART_STM32_BUF_SIZE,
                                         UART_STM32_BUF_SIZE,
                                         0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_STM32_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_STM32_NUM,
                                  UART_STM32_TXD, UART_STM32_RXD,
                                  UART_STM32_RTS, UART_STM32_CTS));
}

/**
 * @brief Send a protocol frame to STM32 over UART.
 */
static bool stm32_send_frame(uint8_t cmd, const uint8_t *payload, uint16_t len) {
    uint8_t buf[PROTO_MAX_FRAME];
    uint16_t total = proto_build_frame(buf, sizeof(buf), cmd, payload, len);
    if (total == 0) {
        ESP_LOGE(TAG, "Could not build UART frame cmd=0x%02X len=%u", cmd, len);
        return false;
    }

    int written = uart_write_bytes(UART_STM32_NUM, buf, total);
    if (written != total) {
        ESP_LOGE(TAG, "UART write short: cmd=0x%02X expected=%u got=%d",
                 cmd, total, written);
        return false;
    }

    return uart_wait_tx_done(UART_STM32_NUM, pdMS_TO_TICKS(3000)) == ESP_OK;
}

/**
 * @brief Wait for a specific command from STM32 with timeout.
 * @return true if matching frame received, false on timeout.
 */
static bool stm32_wait_cmd(uint8_t expected_cmd, ProtoFrame_t *out,
                            uint32_t timeout_ms) {
    ProtoParser_t parser;
    proto_parser_init(&parser);

    uint8_t byte;
    uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while ((xTaskGetTickCount() * portTICK_PERIOD_MS) - start < timeout_ms) {
        int len = uart_read_bytes(UART_STM32_NUM, &byte, 1,
                                  pdMS_TO_TICKS(50));
        if (len > 0) {
            const ProtoFrame_t *f = proto_parser_feed(&parser, byte);
            if (f != NULL) {
                if (f->cmd == expected_cmd) {
                    if (out) memcpy(out, f, sizeof(ProtoFrame_t));
                    return true;
                }
                if (f->cmd == CMD_NAK) {
                    /* NAK received — caller should handle */
                    if (out) memcpy(out, f, sizeof(ProtoFrame_t));
                    return false;
                }
            }
        }
    }

    return false;
}

/*---------------------------------------------------------------------------
 * SPIFFS (firmware storage)
 *---------------------------------------------------------------------------*/

static void spiffs_init(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/spiffs",
        .partition_label        = "storage",
        .max_files              = 5,
        .format_if_mount_failed = true
    };

    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));

    size_t total = 0, used = 0;
    esp_spiffs_info(conf.partition_label, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: total=%d, used=%d", total, used);
}

/*---------------------------------------------------------------------------
 * Firmware staging integrity
 *---------------------------------------------------------------------------*/

static void discard_incoming_firmware(void) {
    if (g_fw_file != NULL) {
        fclose(g_fw_file);
        g_fw_file = NULL;
    }
    g_fw_staged = false;
    g_fw_receiving = false;
    g_fw_size = 0;
    g_fw_expected_size = 0;
    g_fw_running_crc = 0;
    unlink(FW_FILE_PATH);
}

static bool stage_firmware_data(const uint8_t *data, size_t len) {
    if (!g_fw_receiving || g_fw_file == NULL || g_fw_expected_size == 0 ||
        g_fw_size > g_fw_expected_size ||
        len > (size_t)(g_fw_expected_size - g_fw_size)) {
        ESP_LOGE(TAG, "Invalid firmware chunk: received=%u staged=%lu expected=%lu",
                 (unsigned)len, g_fw_size, g_fw_expected_size);
        discard_incoming_firmware();
        return false;
    }

    size_t written = fwrite(data, 1, len, g_fw_file);
    if (written != len) {
        ESP_LOGE(TAG, "SPIFFS write short: expected=%u got=%u",
                 (unsigned)len, (unsigned)written);
        discard_incoming_firmware();
        return false;
    }

    g_fw_running_crc = proto_crc32(data, len, g_fw_running_crc);
    g_fw_size += (uint32_t)len;
    if (g_fw_size == g_fw_expected_size) {
        if (fclose(g_fw_file) != 0) {
            g_fw_file = NULL;
            ESP_LOGE(TAG, "Failed to close staged firmware file");
            discard_incoming_firmware();
            return false;
        }
        g_fw_file = NULL;
        g_fw_receiving = false;
    }
    return true;
}

static bool verify_staged_firmware(void) {
    if (g_fw_receiving || g_fw_size == 0 ||
        g_fw_size != g_fw_expected_size) {
        ESP_LOGE(TAG, "Firmware stage is incomplete");
        return false;
    }

    struct stat st;
    if (stat(FW_FILE_PATH, &st) != 0 || st.st_size < 0 ||
        (uint32_t)st.st_size != g_fw_size) {
        ESP_LOGE(TAG, "Firmware file size mismatch");
        return false;
    }

    FILE *f = fopen(FW_FILE_PATH, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open staged firmware for verification");
        return false;
    }

    uint8_t buffer[256];
    uint32_t crc = 0U;
    uint32_t total = 0U;
    while (true) {
        size_t count = fread(buffer, 1, sizeof(buffer), f);
        if (count == 0) {
            break;
        }
        total += (uint32_t)count;
        crc = proto_crc32(buffer, count, crc);
    }
    bool read_ok = !ferror(f);
    fclose(f);

    if (!read_ok || total != g_fw_size || crc != g_fw_crc32) {
        ESP_LOGE(TAG, "Firmware verification failed: size=%lu/%lu crc=0x%08lX/0x%08lX",
                 total, g_fw_size, crc, g_fw_crc32);
        return false;
    }

    return true;
}

/*---------------------------------------------------------------------------
 * Bluetooth SPP
 *---------------------------------------------------------------------------*/

static void spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
    switch (event) {
    case ESP_SPP_INIT_EVT:
        ESP_LOGI(TAG, "SPP init");
        ESP_ERROR_CHECK(esp_spp_start_srv(ESP_SPP_SEC_AUTHENTICATE,
                                          ESP_SPP_ROLE_SLAVE,
                                          0, SPP_SERVER_NAME));
        break;

    case ESP_SPP_START_EVT:
        if (param->start.status == ESP_SPP_SUCCESS) {
            ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                                     ESP_BT_GENERAL_DISCOVERABLE));
            ESP_LOGI(TAG, "SPP server discoverable: %s", SPP_SERVER_NAME);
        } else {
            ESP_LOGE(TAG, "Failed to start SPP server: status=%d",
                     param->start.status);
        }
        break;

    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(TAG, "SPP client connected, handle=%u", param->srv_open.handle);
        g_spp_handle = param->srv_open.handle;
        break;

    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(TAG, "SPP closed, handle=%u", param->close.handle);
        g_spp_handle = 0;
        g_cmd_len = 0;
        if (g_fw_receiving) {
            ESP_LOGW(TAG, "Discarding incomplete firmware after SPP disconnect");
            g_fw_staged = false;
            g_fw_receiving = false;
            g_fw_size = 0;
            g_fw_expected_size = 0;
        }
        break;

    case ESP_SPP_DATA_IND_EVT:
        /* Forward received BT data to bt_recv_task via queue.
         * Carries length explicitly: binary firmware chunks may contain
         * embedded NUL bytes, so strlen() is not usable. */
        if (param->data_ind.len > 0) {
            uint8_t *copy = (uint8_t *)malloc(param->data_ind.len + 1);
            if (copy) {
                memcpy(copy, param->data_ind.data, param->data_ind.len);
                copy[param->data_ind.len] = '\0';
                SppData_t item = { .data = copy, .len = param->data_ind.len };
                if (xQueueSend(g_spp_queue, &item, 0) != pdPASS) {
                    ESP_LOGW(TAG, "SPP RX queue full; dropping %u bytes",
                             (unsigned)item.len);
                    free(copy);
                }
            }
        }
        break;

    default:
        break;
    }
}

static void bt_spp_init(void) {
    g_spp_queue = xQueueCreate(64, sizeof(SppData_t));

    /* sdkconfig selects Classic-only SPP, so BLE RAM is not needed. */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_spp_register_callback(spp_callback));

    esp_spp_cfg_t spp_cfg = {};
    spp_cfg.mode = ESP_SPP_MODE_CB;
    ESP_ERROR_CHECK(esp_spp_enhanced_init(&spp_cfg));

    /* Set discoverable device name (IDF 6 API) */
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name(SPP_SERVER_NAME));
}

/**
 * @brief Send data back to the connected SPP client.
 */
static void bt_spp_send(const uint8_t *data, size_t len) {
    if (g_spp_handle != 0) {
        esp_spp_write(g_spp_handle, len, (uint8_t *)data);
    }
}

/**
 * @brief Send a text string back to the SPP client.
 */
static void bt_spp_print(const char *msg) {
    bt_spp_send((const uint8_t *)msg, strlen(msg));
}

/*---------------------------------------------------------------------------
 * BT receive task — processes incoming data from phone
 *---------------------------------------------------------------------------*/

static void bt_recv_task(void *pv) {
    SppData_t item;

    for (;;) {
        if (xQueueReceive(g_spp_queue, &item, portMAX_DELAY) == pdPASS) {
            uint8_t *data = item.data;
            size_t   len  = item.len;

            /* Reassemble a terminal command. SPP does not preserve the
             * write boundaries of the PC serial terminal. */
            if (g_cmd_len > 0 ||
                (len > 0 && ((data[0] >= 'A' && data[0] <= 'Z') ||
                             (data[0] >= 'a' && data[0] <= 'z')))) {
                bool command_ready = false;

                for (size_t i = 0; i < len; i++) {
                    if (data[i] == '\r' || data[i] == '\n') {
                        if (g_cmd_len > 0) {
                            uint8_t *line = (uint8_t *)malloc(g_cmd_len + 1);
                            if (line) {
                                memcpy(line, g_cmd_buf, g_cmd_len);
                                line[g_cmd_len] = '\0';
                                free(data);
                                data = line;
                                len = g_cmd_len;
                                command_ready = true;
                            }
                            g_cmd_len = 0;
                        }
                        break;
                    }

                    if (data[i] < 0x20 || data[i] > 0x7E ||
                        g_cmd_len >= sizeof(g_cmd_buf) - 1) {
                        g_cmd_len = 0;
                        break;
                    }
                    g_cmd_buf[g_cmd_len++] = (char)data[i];
                }

                if (!command_ready) {
                    free(data);
                    continue;
                }
            }

            /* Simple text command detection: starts with ASCII letter */
            if (len > 0 && ((data[0] >= 'A' && data[0] <= 'Z') ||
                            (data[0] >= 'a' && data[0] <= 'z'))) {
                char cmd[256] = {0};
                size_t n = len < sizeof(cmd) - 1 ? len : sizeof(cmd) - 1;
                memcpy(cmd, data, n);
                cmd[n] = '\0';

                if (strncmp(cmd, "DATA ", 5) != 0 && strncmp(cmd, "data ", 5) != 0) {
                    ESP_LOGI(TAG, "BT cmd: %s", cmd);
                }

                if (strncmp(cmd, "OTA ", 4) == 0 || strncmp(cmd, "ota ", 4) == 0) {
                    /* Extract URL (http:// or https://) */
                    char *url = cmd + 4;
                    while (*url == ' ') url++;

                    bt_spp_print("STATUS: Downloading firmware...\r\n");

                    if (download_firmware_http(url)) {
                        bt_spp_print("STATUS: Download OK, transferring...\r\n");
                        if (transfer_to_stm32()) {
                            bt_spp_print("STATUS: OTA complete!\r\n");
                        } else {
                            bt_spp_print("STATUS: Transfer failed\r\n");
                        }
                    } else {
                        bt_spp_print("STATUS: Download failed\r\n");
                    }

                } else if (strncmp(cmd, "FW ", 3) == 0 || strncmp(cmd, "fw ", 3) == 0) {
                    /* Begin a Bluetooth firmware push. DATA carries Base64
                     * so each printable block can be offset-acknowledged and
                     * retried independently before final CRC verification. */
                    unsigned long ver = 0, size = 0, crc = 0;
                    if (sscanf(cmd + 3, "%lu,%lu,%lx", &ver, &size, &crc) == 3 &&
                        size > 0 && size <= FW_FILE_MAX_SIZE) {
                        discard_incoming_firmware();
                        g_fw_version = (uint32_t)ver;
                        g_fw_expected_size = (uint32_t)size;
                        g_fw_crc32   = (uint32_t)crc;
                        g_fw_running_crc = 0;
                        g_fw_file = fopen(FW_FILE_PATH, "wb");
                        if (g_fw_file != NULL) {
                            g_fw_receiving = true;

                            char msg[96];
                            snprintf(msg, sizeof(msg),
                                     "FW: ready for %lu bytes, send DATA blocks\r\n",
                                     g_fw_expected_size);
                            bt_spp_print(msg);
                        } else {
                            ESP_LOGE(TAG, "Cannot open staged firmware for writing");
                            discard_incoming_firmware();
                            bt_spp_print("FW: storage open failed\r\n");
                        }
                    } else {
                        bt_spp_print("FW: usage FW <version>,<size>,<crc32-hex>; max 55296 bytes\r\n");
                    }

                } else if (strncmp(cmd, "DATA ", 5) == 0 || strncmp(cmd, "data ", 5) == 0) {
                    char *offset_text = cmd + 5;
                    char *comma = strchr(offset_text, ',');
                    bool accepted = false;

                    if (comma != NULL && g_fw_expected_size > 0 && !g_fw_staged) {
                        *comma = '\0';
                        char *end = NULL;
                        unsigned long offset = strtoul(offset_text, &end, 10);
                        const char *encoded = comma + 1;
                        uint8_t decoded[192];
                        size_t decoded_len = 0;

                        int rc = mbedtls_base64_decode(
                            decoded, sizeof(decoded), &decoded_len,
                            (const unsigned char *)encoded, strlen(encoded));

                        if (end != offset_text && *end == '\0' && rc == 0 &&
                            decoded_len > 0 &&
                            offset <= g_fw_expected_size &&
                            decoded_len <= g_fw_expected_size - offset) {
                            if (offset == g_fw_size && g_fw_size < g_fw_expected_size) {
                                accepted = stage_firmware_data(decoded, decoded_len);
                            } else if (offset + decoded_len == g_fw_size) {
                                /* The previous ACK may have been lost. Do not
                                 * append the same block twice; ACK it again. */
                                accepted = true;
                            }
                        }
                    }

                    if (accepted) {
                        char msg[48];
                        snprintf(msg, sizeof(msg), "DATA: ACK %lu\r\n", g_fw_size);
                        bt_spp_print(msg);
                    } else {
                        char msg[64];
                        snprintf(msg, sizeof(msg), "DATA: NAK expected offset %lu\r\n",
                                 g_fw_size);
                        bt_spp_print(msg);
                    }

                } else if (strncmp(cmd, "VERIFY", 6) == 0 || strncmp(cmd, "verify", 6) == 0) {
                    if (!g_fw_receiving && g_fw_size == g_fw_expected_size) {
                        g_fw_staged = true;
                        if (g_fw_running_crc != g_fw_crc32) {
                            ESP_LOGE(TAG,
                                     "Firmware receive CRC failed: crc=0x%08lX/0x%08lX",
                                     g_fw_running_crc, g_fw_crc32);
                            g_fw_staged = false;
                            bt_spp_print("FW: CRC mismatch; restart with FW\r\n");
                        } else if (verify_staged_firmware()) {
                            char msg[96];
                            snprintf(msg, sizeof(msg),
                                     "FW: staged %lu bytes; send SEND to start OTA\r\n",
                                     g_fw_size);
                            bt_spp_print(msg);
                        } else {
                            g_fw_staged = false;
                            bt_spp_print("FW: CRC mismatch; restart with FW\r\n");
                        }
                    } else {
                        bt_spp_print("FW: incomplete; continue DATA blocks\r\n");
                    }

                } else if (strncmp(cmd, "SEND", 4) == 0 || strncmp(cmd, "send", 4) == 0) {
                    /* Transfer the staged (Bluetooth-pushed) firmware */
                    if (g_fw_staged) {
                        bt_spp_print("STATUS: Transferring...\r\n");
                        if (transfer_to_stm32()) {
                            bt_spp_print("STATUS: OTA complete!\r\n");
                        } else {
                            bt_spp_print("STATUS: Transfer failed\r\n");
                        }
                    } else {
                        bt_spp_print("STATUS: No verified firmware staged (use FW, DATA, VERIFY)\r\n");
                    }

                } else if (strncmp(cmd, "WIFI ", 5) == 0 || strncmp(cmd, "wifi ", 5) == 0) {
                    /* WIFI <ssid>,<password> — store to NVS and reconnect */
                    char ssid[33] = {0};
                    char pass[65] = {0};
                    if (sscanf(cmd + 5, "%32[^,],%64s", ssid, pass) == 2) {
                        nvs_handle_t h;
                        if (nvs_open("cfg", NVS_READWRITE, &h) == ESP_OK) {
                            nvs_set_str(h, "wifi_ssid", ssid);
                            nvs_set_str(h, "wifi_pass", pass);
                            nvs_commit(h);
                            nvs_close(h);
                        }
                        wifi_connect(ssid, pass);
                        bt_spp_print("WIFI: saved and reconnecting\r\n");
                    } else {
                        bt_spp_print("WIFI: usage WIFI <ssid>,<password>\r\n");
                    }

                } else if (strncmp(cmd, "VERSION", 7) == 0 || strncmp(cmd, "version", 7) == 0) {
                    /* Query STM32 version */
                    ProtoFrame_t resp;
                    if (stm32_send_frame(CMD_GET_STATUS, NULL, 0) &&
                        stm32_wait_cmd(CMD_STATUS_RSP, &resp, 1500)) {
                        char buf[64];
                        uint32_t ver = 0;
                        if (resp.len >= 4) memcpy(&ver, resp.payload, 4);
                        snprintf(buf, sizeof(buf), "FW Version: %lu\r\n", ver);
                        bt_spp_print(buf);
                    } else {
                        bt_spp_print("VERSION: No response from STM32\r\n");
                    }

                } else if (strncmp(cmd, "STATUS", 6) == 0 || strncmp(cmd, "status", 6) == 0) {
                    wifi_ap_record_t ap_info;
                    bool wifi_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
                    char buf[192];
                    snprintf(buf, sizeof(buf),
                             "Bridge Status:\r\n"
                             "  BT connected: %s\r\n"
                             "  WiFi connected: %s\r\n"
                             "  FW receiving: %s\r\n"
                             "  FW staged: %s\r\n"
                             "  Staged size: %lu / %lu bytes\r\n"
                             "  Staged version: %lu\r\n",
                             g_spp_handle ? "yes" : "no",
                             wifi_connected ? "yes" : "no",
                             g_fw_receiving ? "yes" : "no",
                             g_fw_staged ? "yes" : "no",
                             g_fw_size,
                             g_fw_expected_size,
                             g_fw_version);
                    bt_spp_print(buf);

                } else if (strncmp(cmd, "RESET", 5) == 0 || strncmp(cmd, "reset", 5) == 0) {
                    bt_spp_print("Resetting...\r\n");
                    vTaskDelay(pdMS_TO_TICKS(100));
                    esp_restart();

                } else {
                    bt_spp_print("Unknown command. Commands: OTA <url>, FW <ver>,<size>,<crc>, DATA <offset>,<base64>, VERIFY, SEND, WIFI <ssid>,<pass>, VERSION, STATUS, RESET\r\n");
                }
            } else {
                bt_spp_print("FW: non-text data rejected; use Base64 DATA blocks\r\n");
            }

            free(data);
        }
    }
}

/*---------------------------------------------------------------------------
 * WiFi HTTP firmware download
 *---------------------------------------------------------------------------*/

static void wifi_connect(const char *ssid, const char *password) {
    wifi_config_t wifi_cfg = {};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_LOGI(TAG, "WiFi connecting to %s...", ssid);
    esp_wifi_connect();
}

static void wifi_init_sta(void) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Credentials: runtime-configured via the "WIFI" BT command (NVS),
     * falling back to compile-time defaults above. */
    nvs_handle_t h;
    char ssid[33] = {0};
    char pass[65] = {0};
    if (nvs_open("cfg", NVS_READONLY, &h) == ESP_OK) {
        size_t slen = sizeof(ssid), plen = sizeof(pass);
        nvs_get_str(h, "wifi_ssid", ssid, &slen);
        nvs_get_str(h, "wifi_pass", pass, &plen);
        nvs_close(h);
    }
    if (ssid[0] == '\0') strncpy(ssid, WIFI_SSID, sizeof(ssid) - 1);
    if (pass[0] == '\0') strncpy(pass, WIFI_PASSWORD, sizeof(pass) - 1);

    wifi_connect(ssid, pass);
}

/*---------------------------------------------------------------------------
 * Firmware version from URL filename
 *---------------------------------------------------------------------------*/

/**
 * @brief Parse the firmware version from a URL like ".../fw_v2.bin".
 *        Falls back to version 1 when the pattern is not found.
 */
static uint32_t parse_version_from_url(const char *url) {
    const char *name = strrchr(url, '/');
    name = (name != NULL) ? name + 1 : url;

    if (strncmp(name, "fw_v", 4) == 0) {
        return (uint32_t)strtoul(name + 4, NULL, 10);
    }
    return 1U;
}

static bool download_firmware_http(const char *url) {
    ESP_LOGI(TAG, "Downloading firmware from: %s", url);

    g_fw_staged = false;
    g_fw_receiving = false;
    g_fw_size = 0;
    g_fw_expected_size = 0;

    FILE *f = fopen(FW_FILE_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open firmware file for writing");
        return false;
    }

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = url;
    http_cfg.timeout_ms = 30000;
    http_cfg.buffer_size = HTTP_DOWNLOAD_BUF_SIZE;

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        fclose(f);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0 || content_length > FW_FILE_MAX_SIZE) {
        ESP_LOGE(TAG, "Invalid content length: %d", content_length);
        esp_http_client_cleanup(client);
        fclose(f);
        return false;
    }

    uint8_t buf[HTTP_DOWNLOAD_BUF_SIZE];
    int total_read = 0;

    while (total_read < content_length) {
        int read_len = esp_http_client_read(client, (char *)buf, sizeof(buf));
        if (read_len <= 0) {
            ESP_LOGE(TAG, "HTTP read error at %d / %d", total_read, content_length);
            esp_http_client_cleanup(client);
            fclose(f);
            return false;
        }

        if (fwrite(buf, 1, read_len, f) != (size_t)read_len) {
            ESP_LOGE(TAG, "HTTP staging write failed at %d / %d",
                     total_read, content_length);
            esp_http_client_cleanup(client);
            fclose(f);
            return false;
        }
        total_read += read_len;

        ESP_LOGI(TAG, "Downloaded %d / %d bytes", total_read, content_length);
    }

    fclose(f);
    esp_http_client_cleanup(client);

    g_fw_size = (uint32_t)total_read;
    g_fw_expected_size = g_fw_size;

    /* Compute CRC-32 of downloaded file */
    f = fopen(FW_FILE_PATH, "rb");
    if (!f) return false;

    uint32_t crc = 0U;
    while (true) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n == 0) break;
        crc = proto_crc32(buf, n, crc);
    }
    fclose(f);
    g_fw_crc32 = crc;

    /* Derive version from the URL filename (fw_v<N>.bin); default 1. */
    g_fw_version = parse_version_from_url(url);

    g_fw_staged = true;

    ESP_LOGI(TAG, "Download complete: %d bytes, CRC32=0x%08lX",
             g_fw_size, g_fw_crc32);
    return true;
}

/*---------------------------------------------------------------------------
 * OTA Transfer to STM32
 *---------------------------------------------------------------------------*/

static bool transfer_to_stm32(void) {
    if (!g_fw_staged || g_fw_receiving) {
        ESP_LOGE(TAG, "No firmware staged for transfer");
        return false;
    }

    if (!verify_staged_firmware()) {
        return false;
    }

    FILE *f = fopen(FW_FILE_PATH, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open firmware file for reading");
        return false;
    }

    ESP_LOGI(TAG, "Starting OTA transfer: size=%lu, CRC32=0x%08lX",
             g_fw_size, g_fw_crc32);

    /* Step 1: ask the running application to persist its OTA request and
     * reboot. CMD_OTA_READY makes the timing deterministic instead of relying
     * on a guessed reset delay. */
    uart_flush_input(UART_STM32_NUM);
    if (!stm32_send_frame(CMD_OTA_AVAILABLE,
                          (const uint8_t *)&g_fw_version, 4)) {
        fclose(f);
        return false;
    }

    ProtoFrame_t resp;
    if (!stm32_wait_cmd(CMD_OTA_READY, &resp, 2000) || resp.len < 4) {
        ESP_LOGE(TAG, "Application did not confirm OTA readiness");
        fclose(f);
        return false;
    }

    uint32_t ready_version = 0;
    memcpy(&ready_version, resp.payload, 4);
    if (ready_version != g_fw_version) {
        ESP_LOGE(TAG, "OTA readiness version mismatch: %lu != %lu",
                 ready_version, g_fw_version);
        fclose(f);
        return false;
    }

    /* The app sends READY, waits 50 ms, then resets. Give the bootloader time
     * to configure HSE/USART1; it will keep a 2 s OTA window open. */
    vTaskDelay(pdMS_TO_TICKS(150));
    uart_flush_input(UART_STM32_NUM);

    /* Step 2: start the bootloader transfer. */
    uint8_t begin_payload[12];
    memcpy(begin_payload,      &g_fw_size,   4);
    memcpy(begin_payload + 4,  &g_fw_version, 4);
    memcpy(begin_payload + 8,  &g_fw_crc32,  4);
    if (!stm32_send_frame(CMD_OTA_BEGIN, begin_payload, 12)) {
        fclose(f);
        return false;
    }

    /* Wait for OTA_BEGIN_ACK */
    if (!stm32_wait_cmd(CMD_OTA_BEGIN_ACK, &resp, 3000)) {
        ESP_LOGE(TAG, "No OTA_BEGIN_ACK from STM32");
        fclose(f);
        return false;
    }

    if (resp.len < 4) {
        ESP_LOGE(TAG, "Malformed OTA_BEGIN_ACK from STM32");
        fclose(f);
        return false;
    }

    uint32_t expected_seq = 0;
    memcpy(&expected_seq, resp.payload, 4);
    if (expected_seq != 0) {
        ESP_LOGE(TAG, "Bootloader expected unexpected first sequence: %lu",
                 expected_seq);
        fclose(f);
        return false;
    }

    /* Step 3: Send chunks */
    uint8_t chunk_payload[PROTO_MAX_PAYLOAD];
    uint32_t seq = 0;
    uint32_t bytes_sent = 0;

    while (bytes_sent < g_fw_size) {
        /* Prepare chunk header (seq) */
        memcpy(chunk_payload, &seq, 4);

        /* Read page data */
        size_t chunk_len = (g_fw_size - bytes_sent) < OTA_CHUNK_SIZE
                           ? (g_fw_size - bytes_sent)
                           : OTA_CHUNK_SIZE;

        size_t n = fread(chunk_payload + 4, 1, chunk_len, f);
        if (n != chunk_len) {
            ESP_LOGE(TAG, "File read error at seq=%lu", seq);
            fclose(f);
            return false;
        }

        /* Send chunk with retry */
        bool acked = false;
        for (int retry = 0; retry < OTA_MAX_RETRIES && !acked; retry++) {
            if (!stm32_send_frame(CMD_OTA_CHUNK, chunk_payload, 4 + n)) {
                ESP_LOGW(TAG, "Chunk %lu: UART write failed, retry %d",
                         seq, retry + 1);
                continue;
            }

            ProtoFrame_t ack_resp;
            if (stm32_wait_cmd(CMD_CHUNK_ACK, &ack_resp, OTA_CHUNK_TIMEOUT_MS) &&
                ack_resp.len >= 4) {
                uint32_t ack_seq;
                memcpy(&ack_seq, ack_resp.payload, 4);
                if (ack_seq == seq) {
                    acked = true;
                }
            } else {
                /* NAK/timeout already consumed inside stm32_wait_cmd; retry */
                ESP_LOGW(TAG, "Chunk %lu: retry %d", seq, retry + 1);
            }
        }

        if (!acked) {
            ESP_LOGE(TAG, "Chunk %lu failed after %d retries", seq, OTA_MAX_RETRIES);
            (void)stm32_send_frame(CMD_OTA_ABORT, NULL, 0);
            fclose(f);
            return false;
        }

        bytes_sent += n;
        seq++;

        if (seq % 10 == 0) {
            ESP_LOGI(TAG, "Progress: %lu / %lu bytes", bytes_sent, g_fw_size);
        }
    }
    fclose(f);

    /* Step 4: Send OTA_END and verify */
    if (!stm32_send_frame(CMD_OTA_END, NULL, 0)) {
        return false;
    }

    if (!stm32_wait_cmd(CMD_OTA_RESULT, &resp, 10000)) {
        ESP_LOGE(TAG, "No OTA_RESULT from STM32");
        return false;
    }

    uint32_t result_code = 0;
    if (resp.len >= 4) memcpy(&result_code, resp.payload, 4);

    if (result_code == OTA_RESULT_OK) {
        ESP_LOGI(TAG, "OTA transfer successful!");
        g_fw_staged = false;
        g_fw_size = 0;
        g_fw_expected_size = 0;
        g_fw_crc32 = 0;
        /* Delete the staged file */
        unlink(FW_FILE_PATH);
        return true;
    } else {
        ESP_LOGE(TAG, "OTA transfer failed: result=%lu", result_code);
        return false;
    }
}

/*---------------------------------------------------------------------------
 * Main
 *---------------------------------------------------------------------------*/

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== STM32 OTA Bridge starting ===");

    /* Init NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Init storage */
    spiffs_init();

    /* Init UART to STM32 */
    uart_stm32_init();

    /* Init Bluetooth SPP */
    bt_spp_init();

    /* Init WiFi (for OTA downloads) */
    wifi_init_sta();

    /* Create BT receive task */
    xTaskCreate(bt_recv_task, "bt_recv", SPP_TASK_STACK, NULL, SPP_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "Bridge ready. BT device: %s", SPP_SERVER_NAME);
    ESP_LOGI(TAG, "Connect via Bluetooth SPP and send commands.");

    /* Main loop — nothing to do, tasks handle everything */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Bridge alive. BT=%s, FW=%s",
                 g_spp_handle ? "connected" : "idle",
                 g_fw_staged ? "staged" : "none");
    }
}

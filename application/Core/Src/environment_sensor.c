#include "environment_sensor.h"

#include "env_i2c.h"
#include "stm32f1xx_hal.h"

#define AHT20_ADDRESS               0x38U
#define AHT20_STATUS_COMMAND        0x71U
#define AHT20_STATUS_CALIBRATED     0x08U
#define AHT20_STATUS_BUSY           0x80U
#define AHT20_CRC_POLYNOMIAL        0x31U

#define BMP280_ADDRESS_LOW          0x76U
#define BMP280_ADDRESS_HIGH         0x77U
#define BMP280_CHIP_ID_REGISTER     0xD0U
#define BMP280_CHIP_ID              0x58U
#define BMP280_CALIB_REGISTER       0x88U
#define BMP280_CTRL_MEAS_REGISTER   0xF4U
#define BMP280_DATA_REGISTER        0xF7U
#define BMP280_FORCED_X1            0x25U

typedef struct {
    uint16_t t1;
    int16_t t2;
    int16_t t3;
    uint16_t p1;
    int16_t p2;
    int16_t p3;
    int16_t p4;
    int16_t p5;
    int16_t p6;
    int16_t p7;
    int16_t p8;
    int16_t p9;
} Bmp280Calibration_t;

static Bmp280Calibration_t g_bmp280_calibration;
static uint8_t g_bmp280_address;
static bool g_aht20_ready;
static bool g_bmp280_ready;

static bool register_read(uint8_t address, uint8_t reg, uint8_t *data,
                          uint16_t length) {
    return env_i2c_write(address, &reg, 1U) &&
           env_i2c_read(address, data, length);
}

static bool register_write(uint8_t address, uint8_t reg, uint8_t value) {
    const uint8_t command[2] = {reg, value};
    return env_i2c_write(address, command, sizeof(command));
}

static uint16_t decode_u16_le(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static int16_t decode_s16_le(const uint8_t *data) {
    return (int16_t)decode_u16_le(data);
}

static uint8_t aht20_crc8(const uint8_t *data, uint8_t length) {
    uint8_t crc = 0xFFU;

    for (uint8_t index = 0U; index < length; index++) {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            crc = (crc & 0x80U) != 0U
                ? (uint8_t)((crc << 1) ^ AHT20_CRC_POLYNOMIAL)
                : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static bool aht20_init(void) {
    uint8_t status;
    const uint8_t status_command = AHT20_STATUS_COMMAND;
    const uint8_t initialize[3] = {0xBEU, 0x08U, 0x00U};

    if (!env_i2c_device_ready(AHT20_ADDRESS)) {
        return false;
    }

    HAL_Delay(100U);
    if (!env_i2c_write(AHT20_ADDRESS, &status_command, 1U) ||
        !env_i2c_read(AHT20_ADDRESS, &status, 1U)) {
        return false;
    }

    if ((status & AHT20_STATUS_CALIBRATED) == 0U) {
        if (!env_i2c_write(AHT20_ADDRESS, initialize, sizeof(initialize))) {
            return false;
        }
        HAL_Delay(10U);
    }
    return true;
}

static bool bmp280_init(void) {
    uint8_t id;
    uint8_t calibration[24];

    if (env_i2c_device_ready(BMP280_ADDRESS_LOW) &&
        register_read(BMP280_ADDRESS_LOW, BMP280_CHIP_ID_REGISTER, &id, 1U) &&
        id == BMP280_CHIP_ID) {
        g_bmp280_address = BMP280_ADDRESS_LOW;
    } else if (env_i2c_device_ready(BMP280_ADDRESS_HIGH) &&
               register_read(BMP280_ADDRESS_HIGH, BMP280_CHIP_ID_REGISTER,
                             &id, 1U) &&
               id == BMP280_CHIP_ID) {
        g_bmp280_address = BMP280_ADDRESS_HIGH;
    } else {
        return false;
    }

    if (!register_read(g_bmp280_address, BMP280_CALIB_REGISTER,
                       calibration, sizeof(calibration))) {
        return false;
    }

    g_bmp280_calibration.t1 = decode_u16_le(&calibration[0]);
    g_bmp280_calibration.t2 = decode_s16_le(&calibration[2]);
    g_bmp280_calibration.t3 = decode_s16_le(&calibration[4]);
    g_bmp280_calibration.p1 = decode_u16_le(&calibration[6]);
    g_bmp280_calibration.p2 = decode_s16_le(&calibration[8]);
    g_bmp280_calibration.p3 = decode_s16_le(&calibration[10]);
    g_bmp280_calibration.p4 = decode_s16_le(&calibration[12]);
    g_bmp280_calibration.p5 = decode_s16_le(&calibration[14]);
    g_bmp280_calibration.p6 = decode_s16_le(&calibration[16]);
    g_bmp280_calibration.p7 = decode_s16_le(&calibration[18]);
    g_bmp280_calibration.p8 = decode_s16_le(&calibration[20]);
    g_bmp280_calibration.p9 = decode_s16_le(&calibration[22]);
    return g_bmp280_calibration.p1 != 0U;
}

static int32_t bmp280_compensate_temperature(int32_t raw_temperature,
                                              int32_t *fine_temperature) {
    const int32_t var1 =
        (((raw_temperature >> 3) -
          ((int32_t)g_bmp280_calibration.t1 << 1)) *
         (int32_t)g_bmp280_calibration.t2) >> 11;
    const int32_t delta =
        (raw_temperature >> 4) - (int32_t)g_bmp280_calibration.t1;
    const int32_t var2 =
        (((delta * delta) >> 12) *
         (int32_t)g_bmp280_calibration.t3) >> 14;

    *fine_temperature = var1 + var2;
    return (*fine_temperature * 5 + 128) >> 8;
}

static uint32_t bmp280_compensate_pressure(int32_t raw_pressure,
                                           int32_t fine_temperature) {
    int64_t var1 = (int64_t)fine_temperature - 128000;
    int64_t var2 = var1 * var1 * (int64_t)g_bmp280_calibration.p6;
    var2 += var1 * (int64_t)g_bmp280_calibration.p5 * 131072;
    var2 += (int64_t)g_bmp280_calibration.p4 * (((int64_t)1) << 35);
    var1 = ((var1 * var1 * (int64_t)g_bmp280_calibration.p3) >> 8) +
           (var1 * (int64_t)g_bmp280_calibration.p2 * 4096);
    var1 = (((((int64_t)1) << 47) + var1) *
            (int64_t)g_bmp280_calibration.p1) >> 33;
    if (var1 == 0) {
        return 0U;
    }

    int64_t pressure = 1048576 - raw_pressure;
    pressure = (((pressure << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)g_bmp280_calibration.p9 *
            (pressure >> 13) * (pressure >> 13)) >> 25;
    var2 = ((int64_t)g_bmp280_calibration.p8 * pressure) >> 19;
    pressure = ((pressure + var1 + var2) >> 8) +
               ((int64_t)g_bmp280_calibration.p7 * 16);
    return (uint32_t)((pressure + 128) >> 8);
}

bool environment_sensor_init(void) {
    g_aht20_ready = aht20_init();
    g_bmp280_ready = bmp280_init();
    return g_aht20_ready && g_bmp280_ready;
}

bool environment_sensor_start_measurement(void) {
    const uint8_t trigger[3] = {0xACU, 0x33U, 0x00U};

    if (!g_aht20_ready || !g_bmp280_ready ||
        !env_i2c_write(AHT20_ADDRESS, trigger, sizeof(trigger))) {
        return false;
    }
    return register_write(g_bmp280_address, BMP280_CTRL_MEAS_REGISTER,
                          BMP280_FORCED_X1);
}

bool environment_sensor_read(EnvironmentReading_t *reading) {
    uint8_t aht_data[7];
    uint8_t bmp_data[6];

    if (reading == NULL || !g_aht20_ready || !g_bmp280_ready ||
        !env_i2c_read(AHT20_ADDRESS, aht_data, sizeof(aht_data)) ||
        (aht_data[0] & AHT20_STATUS_BUSY) != 0U ||
        aht20_crc8(aht_data, 6U) != aht_data[6] ||
        !register_read(g_bmp280_address, BMP280_DATA_REGISTER,
                       bmp_data, sizeof(bmp_data))) {
        return false;
    }

    const uint32_t raw_humidity =
        ((uint32_t)aht_data[1] << 12) |
        ((uint32_t)aht_data[2] << 4) |
        ((uint32_t)aht_data[3] >> 4);
    const uint32_t raw_temperature =
        (((uint32_t)aht_data[3] & 0x0FU) << 16) |
        ((uint32_t)aht_data[4] << 8) |
        (uint32_t)aht_data[5];
    const int32_t bmp_raw_pressure =
        (int32_t)(((uint32_t)bmp_data[0] << 12) |
                  ((uint32_t)bmp_data[1] << 4) |
                  ((uint32_t)bmp_data[2] >> 4));
    const int32_t bmp_raw_temperature =
        (int32_t)(((uint32_t)bmp_data[3] << 12) |
                  ((uint32_t)bmp_data[4] << 4) |
                  ((uint32_t)bmp_data[5] >> 4));
    int32_t fine_temperature;

    (void)bmp280_compensate_temperature(bmp_raw_temperature,
                                        &fine_temperature);
    reading->temperature_centi_c =
        (int16_t)(((int64_t)raw_temperature * 20000 + 524288) /
                  1048576 - 5000);
    reading->humidity_centi_percent =
        (uint16_t)(((uint64_t)raw_humidity * 10000U + 524288U) /
                   1048576U);
    reading->pressure_pa =
        bmp280_compensate_pressure(bmp_raw_pressure, fine_temperature);
    return reading->pressure_pa != 0U;
}

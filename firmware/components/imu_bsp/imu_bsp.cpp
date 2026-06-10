#include "imu_bsp.hpp"
#include "power_bsp.h"             // power_bsp_i2c_bus() -> I2C0
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char* TAG = "imu_bsp";
static const int   kTimeoutMs = 1000;
#define QMI8658_ADDR 0x6b
static i2c_master_dev_handle_t s_imu = NULL;

static void wr(uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    i2c_master_transmit(s_imu, b, 2, kTimeoutMs);
}

void imu_init() {
    if (s_imu) return;
    i2c_master_bus_handle_t bus = power_bsp_i2c_bus();
    if (!bus) { ESP_LOGW(TAG, "I2C0 bus not ready"); return; }
    i2c_device_config_t dev = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                .device_address = QMI8658_ADDR, .scl_speed_hz = 100000 };
    if (i2c_master_bus_add_device(bus, &dev, &s_imu) != ESP_OK) {
        ESP_LOGW(TAG, "add QMI8658 failed"); s_imu = NULL; return;
    }
    wr(0x02, 0x40);   // CTRL1: ADDR_AI (auto-increment) for burst reads
    wr(0x03, 0x04);   // CTRL2: accel +-2g, mid ODR
    wr(0x08, 0x01);   // CTRL7: enable accelerometer
    ESP_LOGI(TAG, "QMI8658 ready");
}

bool imu_read_accel(int16_t& ax, int16_t& ay, int16_t& az) {
    if (!s_imu) return false;
    uint8_t reg = 0x35, b[6];
    if (i2c_master_transmit_receive(s_imu, &reg, 1, b, 6, kTimeoutMs) != ESP_OK) return false;
    ax = (int16_t)(b[0] | (b[1] << 8));
    ay = (int16_t)(b[2] | (b[3] << 8));
    az = (int16_t)(b[4] | (b[5] << 8));
    return true;
}

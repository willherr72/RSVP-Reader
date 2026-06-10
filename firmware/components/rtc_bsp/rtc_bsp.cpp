#include "rtc_bsp.hpp"
#include "power_bsp.h"             // power_bsp_i2c_bus() -> I2C0, where the PCF85063 lives
#include "driver/i2c_master.h"
#include "esp_log.h"

using rsvp::RtcTime;
static const char* TAG = "rtc_bsp";
static const int   kTimeoutMs = 1000;
#define PCF85063_ADDR 0x51
static i2c_master_dev_handle_t s_rtc_dev = NULL;

void rtc_init() {
    if (s_rtc_dev) return;
    i2c_master_bus_handle_t bus = power_bsp_i2c_bus();
    if (!bus) { ESP_LOGW(TAG, "I2C0 bus not ready"); return; }
    i2c_device_config_t dev = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                .device_address = PCF85063_ADDR, .scl_speed_hz = 100000 };
    if (i2c_master_bus_add_device(bus, &dev, &s_rtc_dev) != ESP_OK) {
        ESP_LOGW(TAG, "add PCF85063 failed"); s_rtc_dev = NULL; return;
    }
    uint8_t ctrl1[2] = { 0x00, 0x00 };   // Control_1 = 24h mode, running
    i2c_master_transmit(s_rtc_dev, ctrl1, 2, kTimeoutMs);
    ESP_LOGI(TAG, "PCF85063 ready");
}

static bool read_regs(uint8_t start, uint8_t* buf, size_t n) {
    if (!s_rtc_dev) return false;
    return i2c_master_transmit_receive(s_rtc_dev, &start, 1, buf, n, kTimeoutMs) == ESP_OK;
}

bool rtc_valid() {
    uint8_t sec = 0;
    if (!read_regs(0x04, &sec, 1)) return false;
    return (sec & 0x80) == 0;            // OS flag clear -> time is valid
}

bool rtc_get(RtcTime& out) {
    uint8_t b[3] = {0};                  // sec(0x04), min(0x05), hour(0x06)
    if (!read_regs(0x04, b, 3)) return false;
    out.second = rsvp::bcdToBin(b[0] & 0x7F);
    out.minute = rsvp::bcdToBin(b[1] & 0x7F);
    out.hour   = rsvp::bcdToBin(b[2] & 0x3F);
    return true;
}

bool rtc_set(const RtcTime& t) {
    if (!s_rtc_dev) return false;
    uint8_t w[4] = { 0x04,
        (uint8_t)(rsvp::binToBcd(t.second) & 0x7F),   // bit7=0 clears OS
        rsvp::binToBcd(t.minute),
        rsvp::binToBcd(t.hour) };
    return i2c_master_transmit(s_rtc_dev, w, 4, kTimeoutMs) == ESP_OK;
}

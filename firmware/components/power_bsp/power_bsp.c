#include "power_bsp.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_io_expander_tca9554.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PWR_HOLD_PIN  IO_EXPANDER_PIN_NUM_6   // TCA9554 P6: HIGH=stay on, LOW=power off
#define PWR_BTN_GPIO  GPIO_NUM_16             // PWR button, active-low
#define I2C0_SCL_GPIO GPIO_NUM_48
#define I2C0_SDA_GPIO GPIO_NUM_47

static const char *TAG = "power_bsp";
static esp_io_expander_handle_t s_io = NULL;
static power_shutdown_cb_t s_shutdown_cb = NULL;

void power_bsp_set_shutdown_cb(power_shutdown_cb_t cb) { s_shutdown_cb = cb; }

void power_off(void)
{
    ESP_LOGI(TAG, "power_off: driving P6 low");
    if (s_io) esp_io_expander_set_level(s_io, PWR_HOLD_PIN, 0);
}

void power_hold(void)
{
    if (s_io) esp_io_expander_set_level(s_io, PWR_HOLD_PIN, 1);
}

static void power_button_task(void *arg)
{
    (void)arg;
    const int kPollMs    = 20;
    const int kLongCount = 1500 / kPollMs;   // ~1.5 s of continuous press = 75 samples
    int  pressed = 0;
    bool released_seen = false;   // boot guard: require one release before arming
    bool fired = false;
    for (;;) {
        bool down = (gpio_get_level(PWR_BTN_GPIO) == 0);   // active-low
        if (!down) {
            released_seen = true;
            pressed = 0;
            fired = false;
        } else if (released_seen && !fired) {
            if (++pressed >= kLongCount) {
                fired = true;
                ESP_LOGI(TAG, "PWR long-press -> shutdown");
                if (s_shutdown_cb) s_shutdown_cb();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }
}

void power_bsp_init(void)
{
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C0_SCL_GPIO,
        .sda_io_num = I2C0_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    if (i2c_new_master_bus(&cfg, &bus) != ESP_OK) {
        ESP_LOGW(TAG, "I2C0 bus init failed; power-hold unavailable");
        return;
    }
    if (esp_io_expander_new_i2c_tca9554(bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &s_io) != ESP_OK) {
        ESP_LOGW(TAG, "TCA9554 init failed; power-hold unavailable");
        s_io = NULL;
        return;
    }
    esp_io_expander_set_dir(s_io, PWR_HOLD_PIN, IO_EXPANDER_OUTPUT);
    power_hold();                                       // P6 high: hold power on
    ESP_LOGI(TAG, "power-hold asserted (TCA9554 P6 high)");

    gpio_config_t btn = {
        .pin_bit_mask = (uint64_t)1 << PWR_BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn);

    xTaskCreatePinnedToCore(power_button_task, "pwrbtn", 3 * 1024, NULL, 5, NULL, 1);
}

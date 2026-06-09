#include "sdcard_bsp.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_io_expander_tca9554.h"
#include "esp_log.h"

#define SDMMC_CLK_PIN   GPIO_NUM_41
#define SDMMC_CMD_PIN   GPIO_NUM_39
#define SDMMC_D0_PIN    GPIO_NUM_40
#define SD_MOUNT_POINT  "/sdcard"

static const char *TAG = "sdcard_bsp";
static sdmmc_card_t *s_card = NULL;

// The board gates the SD card through a TCA9554 I/O expander on I2C0 (GPIO48/47):
// pin P1 must be driven low to enable the SD bus before mounting. Without it the
// SDMMC reads nothing and f_mount returns FR_NO_FILESYSTEM (13). Matches the
// Waveshare 04_SD_Card example. (When RTC/IMU/battery arrive they share this I2C0
// bus + expander — factor this into a shared io-expander bring-up then.)
static void sd_power_enable(void)
{
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = GPIO_NUM_48,
        .sda_io_num = GPIO_NUM_47,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    if (i2c_new_master_bus(&cfg, &bus) != ESP_OK) {
        ESP_LOGW(TAG, "TCA9554 I2C0 bus init failed");
        return;
    }
    esp_io_expander_handle_t io = NULL;
    if (esp_io_expander_new_i2c_tca9554(bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io) != ESP_OK) {
        ESP_LOGW(TAG, "TCA9554 init failed");
        return;
    }
    esp_io_expander_set_dir(io, IO_EXPANDER_PIN_NUM_1, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io, IO_EXPANDER_PIN_NUM_1, 0);
    ESP_LOGI(TAG, "SD enabled via TCA9554 P1");
}

void sdcard_init(void)
{
    sd_power_enable();

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = SDMMC_CLK_PIN;
    slot_config.cmd = SDMMC_CMD_PIN;
    slot_config.d0  = SDMMC_D0_PIN;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config,
                                            &mount_config, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
        return;
    }
    ESP_LOGI(TAG, "SD mounted at %s", SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_card);
}

bool sdcard_mounted(void) { return s_card != NULL; }

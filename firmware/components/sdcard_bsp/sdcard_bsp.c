#include "sdcard_bsp.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

#define SDMMC_CLK_PIN   GPIO_NUM_41
#define SDMMC_CMD_PIN   GPIO_NUM_39
#define SDMMC_D0_PIN    GPIO_NUM_40
#define SD_MOUNT_POINT  "/sdcard"

static const char *TAG = "sdcard_bsp";
static sdmmc_card_t *s_card = NULL;

// SDMMC 1-line mount at /sdcard. The SD bus is powered by default on this board, so
// no I/O-expander setup is needed; the earlier mount failures were exFAT (FatFs
// FF_FS_EXFAT=1, see docs/firmware-notes.md), not power. (An earlier TCA9554 SD-power
// init turned out to be unnecessary AND it cut LCD power a few seconds after boot.)
void sdcard_init(void)
{
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

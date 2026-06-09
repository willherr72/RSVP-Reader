#include "app_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char* TAG = "settings";
static const char* NS  = "rsvp";
static Settings s_settings;

Settings& settings() { return s_settings; }

void settings_load()
{
    static bool nvs_ready = false;
    if (!nvs_ready) {
        esp_err_t e = nvs_flash_init();
        if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase(); nvs_flash_init();
        }
        nvs_ready = true;
    }
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) { ESP_LOGI(TAG, "no saved settings; defaults"); return; }
    int32_t v;
    if (nvs_get_i32(h, "wpm",  &v) == ESP_OK) s_settings.wpm = v;
    if (nvs_get_i32(h, "font", &v) == ESP_OK) s_settings.font = (FontSize)v;
    if (nvs_get_i32(h, "bri",  &v) == ESP_OK) s_settings.brightness = v;
    uint8_t b;
    if (nvs_get_u8(h, "flank", &b) == ESP_OK) s_settings.show_flankers = b;
    if (nvs_get_u8(h, "resume", &b) == ESP_OK) s_settings.resume_on_open = b;
    nvs_close(h);
    ESP_LOGI(TAG, "loaded: wpm=%d font=%d bri=%d flank=%d resume=%d",
             s_settings.wpm, s_settings.font, s_settings.brightness,
             s_settings.show_flankers, s_settings.resume_on_open);
}

void settings_save()
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) { ESP_LOGW(TAG, "save: nvs_open failed"); return; }
    nvs_set_i32(h, "wpm",  s_settings.wpm);
    nvs_set_i32(h, "font", s_settings.font);
    nvs_set_i32(h, "bri",  s_settings.brightness);
    nvs_set_u8 (h, "flank", s_settings.show_flankers);
    nvs_set_u8 (h, "resume", s_settings.resume_on_open);
    nvs_commit(h);
    nvs_close(h);
}

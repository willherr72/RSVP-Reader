#include "battery_bsp.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char* TAG = "battery_bsp";
static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;

void batt_init(void) {
    if (s_adc) return;
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) { s_adc = NULL; return; }
    adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    adc_oneshot_config_channel(s_adc, ADC_CHANNEL_3, &ccfg);
    adc_cali_curve_fitting_config_t cal = { .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_3,
                                            .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) { s_cali = NULL; }
    ESP_LOGI(TAG, "battery ADC ready (cali=%d)", s_cali != NULL);
}

int batt_read_mv(void) {
    if (!s_adc || !s_cali) return 0;
    long sum = 0; int ok = 0;
    for (int i = 0; i < 16; i++) {
        int raw = 0, mv = 0;
        if (adc_oneshot_read(s_adc, ADC_CHANNEL_3, &raw) != ESP_OK) continue;
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) continue;
        sum += mv; ok++;
    }
    if (ok == 0) return 0;
    return (int)((sum / ok) * 3);   // /3 divider -> battery mV
}

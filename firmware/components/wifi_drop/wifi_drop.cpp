#include "wifi_drop.h"
#include <cstring>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char* TAG = "wifi_drop";
#define AP_SSID "RSVP-Reader"
#define AP_PASS "rsvpdrop8"

static httpd_handle_t s_httpd = nullptr;
static esp_netif_t*   s_ap_netif = nullptr;
static bool           s_sys_inited = false;

static esp_err_t root_get(httpd_req_t* req) {
    const char* page = "<!doctype html><meta name=viewport content='width=device-width'>"
                       "<h2>RSVP-Reader</h2><p>WiFi Drop is up.</p>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static void start_httpd() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 8;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) { s_httpd = nullptr; return; }
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get, .user_ctx = nullptr };
    httpd_register_uri_handler(s_httpd, &root);
}

void wifi_drop_start(wifi_drop_info_t* out) {
    if (out) std::memset(out, 0, sizeof(*out));
    if (!s_sys_inited) { esp_netif_init(); esp_event_loop_create_default(); s_sys_inited = true; }
    s_ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&icfg) != ESP_OK) { ESP_LOGW(TAG, "wifi_init failed"); return; }
    wifi_config_t wcfg = {};
    std::strncpy((char*)wcfg.ap.ssid, AP_SSID, sizeof(wcfg.ap.ssid));
    wcfg.ap.ssid_len = std::strlen(AP_SSID);
    std::strncpy((char*)wcfg.ap.password, AP_PASS, sizeof(wcfg.ap.password));
    wcfg.ap.channel = 1;
    wcfg.ap.max_connection = 2;
    wcfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wcfg);
    if (esp_wifi_start() != ESP_OK) { ESP_LOGW(TAG, "wifi_start failed"); return; }
    start_httpd();
    esp_netif_ip_info_t ipinfo = {};
    if (s_ap_netif && esp_netif_get_ip_info(s_ap_netif, &ipinfo) == ESP_OK)
        ESP_LOGI(TAG, "httpd=%p AP IP=" IPSTR, (void*)s_httpd, IP2STR(&ipinfo.ip));
    if (out) {
        std::strncpy(out->ssid, AP_SSID, sizeof(out->ssid) - 1);
        std::strncpy(out->pass, AP_PASS, sizeof(out->pass) - 1);
        std::strncpy(out->url, "http://192.168.4.1", sizeof(out->url) - 1);
        out->ok = (s_httpd != nullptr);
    }
    ESP_LOGI(TAG, "AP up: %s", AP_SSID);
}

void wifi_drop_stop(void) {
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = nullptr; }
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_ap_netif) { esp_netif_destroy_default_wifi(s_ap_netif); s_ap_netif = nullptr; }
}

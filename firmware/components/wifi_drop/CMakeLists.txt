idf_component_register(
    SRCS "wifi_drop.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_http_server esp_netif esp_event nvs_flash sdcard_bsp core)

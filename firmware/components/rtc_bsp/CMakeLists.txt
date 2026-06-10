idf_component_register(
    SRCS "rtc_bsp.cpp"
    INCLUDE_DIRS "."
    REQUIRES power_bsp core esp_driver_i2c)

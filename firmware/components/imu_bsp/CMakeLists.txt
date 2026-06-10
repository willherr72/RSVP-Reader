idf_component_register(
    SRCS "imu_bsp.cpp"
    INCLUDE_DIRS "."
    REQUIRES power_bsp esp_driver_i2c)

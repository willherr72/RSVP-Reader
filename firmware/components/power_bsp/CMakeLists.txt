idf_component_register(
    SRCS "power_bsp.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c esp_driver_gpio esp_io_expander esp_io_expander_tca9554)

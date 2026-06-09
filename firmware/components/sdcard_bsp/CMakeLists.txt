idf_component_register(
    SRCS "sdcard_bsp.c"
    INCLUDE_DIRS "."
    REQUIRES fatfs sdmmc esp_driver_sdmmc esp_driver_i2c esp_io_expander esp_io_expander_tca9554)

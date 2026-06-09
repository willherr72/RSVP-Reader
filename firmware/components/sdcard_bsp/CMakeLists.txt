idf_component_register(
    SRCS "sdcard_bsp.c"
    INCLUDE_DIRS "."
    REQUIRES fatfs sdmmc esp_driver_sdmmc)

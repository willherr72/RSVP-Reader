#ifndef POWER_BSP_H
#define POWER_BSP_H
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*power_shutdown_cb_t)(void);

// Create I2C0 (GPIO48/47) + TCA9554 (addr 000), assert expander P6 HIGH (power-hold),
// configure the PWR button (GPIO16), and start the button monitor task. Call FIRST in
// app_main, before the LCD/SPI setup, so battery power latches before the hardware times out.
void power_bsp_init(void);

// Drive expander P6 low -> board powers off (on battery; on USB it just cuts panel power).
void power_off(void);

// Register a callback fired on a ~1.5s PWR long-press. Runs in the button task context
// and must NOT touch LVGL directly.
void power_bsp_set_shutdown_cb(power_shutdown_cb_t cb);

#ifdef __cplusplus
}
#endif
#endif  // POWER_BSP_H

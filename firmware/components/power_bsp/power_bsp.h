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

// Drive expander P6 low -> board powers off (on battery; on USB the board stays alive).
void power_off(void);

// Re-assert the power-hold (P6 high). Used to cancel a power-off that didn't take effect
// (i.e., the board is externally/USB powered) so the device doesn't get stuck.
void power_hold(void);

// Register a callback fired on a ~1.5s PWR long-press. Runs in the button task context
// and must NOT touch LVGL directly.
void power_bsp_set_shutdown_cb(power_shutdown_cb_t cb);

// Register a callback fired on a short press of the BOOT button (GPIO0): the menu/back
// action. Runs in the button task; must NOT touch LVGL directly.
void power_bsp_set_boot_cb(power_shutdown_cb_t cb);

#ifdef __cplusplus
}
#endif
#endif  // POWER_BSP_H

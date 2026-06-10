#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Register the BOOT-button callback + the navigation timer. Call once at startup
// under the LVGL lock.
void ui_menu_init(void);

// Show the menu (the three tiles). Used as the boot screen once boot->menu lands.
void ui_menu_open(void);

#ifdef __cplusplus
}
#endif

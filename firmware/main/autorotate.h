#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void autorotate_start(void);   // start the IMU orientation timer (call once at boot, under LVGL lock)
#ifdef __cplusplus
}
#endif

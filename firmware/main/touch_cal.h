#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Load the touch calibration from NVS (keeps the built-in defaults if none saved).
void touch_cal_load(void);

// Current raw driver coordinates (for the calibration routine to sample taps).
void touch_get_raw(uint16_t* x, uint16_t* y);

// Set + persist the affine calibration: logical_x = c[0]*rx + c[1]*ry + c[2],
// logical_y = c[3]*rx + c[4]*ry + c[5].
void touch_set_calibration(const float coef[6]);

#ifdef __cplusplus
}
#endif

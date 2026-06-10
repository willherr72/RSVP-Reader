#include "autorotate.h"
#include "imu_bsp.hpp"
#include "rsvp/orient.hpp"
#include "app_settings.h"
#include "lvgl.h"

using namespace rsvp;

static ScreenOrient g_orient = ORIENT_NORMAL;
static ScreenOrient g_pending = ORIENT_NORMAL;
static int g_count = 0;
static const int DEADZONE = 4000;   // raw counts (~1/4 g at +-2g)

static void orient_timer_cb(lv_timer_t*) {
    if (!settings().auto_rotate) return;            // locked
    int16_t ax, ay, az;
    if (!imu_read_accel(ax, ay, az)) return;
    int axis = ay;   // calibrated (Task 2): +ay = normal, -ay = flipped
    ScreenOrient want = orientFromAxis(axis, DEADZONE, g_orient);
    if (want == g_orient) { g_count = 0; return; }
    if (want == g_pending) g_count++; else { g_pending = want; g_count = 1; }
    if (g_count >= 3) {                              // ~0.6s hold at 5Hz
        g_orient = want; g_count = 0;
        lv_display_set_rotation(lv_display_get_default(),
            g_orient == ORIENT_NORMAL ? LV_DISPLAY_ROTATION_90 : LV_DISPLAY_ROTATION_270);
    }
}

extern "C" void autorotate_start(void) {
    lv_timer_create(orient_timer_cb, 200, nullptr);   // 5Hz
}

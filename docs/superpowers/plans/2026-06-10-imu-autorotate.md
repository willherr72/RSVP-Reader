# IMU Auto-Rotate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Auto-flip the UI 180° (display rotation 90↔270) when the device is turned over, with a Settings lock.

**Architecture:** Pure orientation decision in host-tested `core/orient`. A C++ `imu_bsp` reads the QMI8658 on `power_bsp`'s I2C0 bus. A 5 Hz `lv_timer` (`autorotate.cpp`) debounces and switches `lv_display_set_rotation`. An `auto_rotate` setting locks it.

**Tech Stack:** ESP-IDF 5.5.2 (`esp_driver_i2c`), LVGL 9, C++17 `core` (doctest).

---

## Conventions

**[HOST]:** `cmake --build "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\build\host" --config Debug` then `& "...\build\host\Debug\rsvp_tests.exe"`.
**[BUILD]/[FLASH]:** `$idfp="C:\Users\WilliamHerr\esp\v5.5.2\esp-idf"; $env:IDF_PATH=$idfp; $env:IDF_PYTHON_ENV_PATH="C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env"; & "$idfp\export.ps1" *> $null; idf.py -C "...\firmware" build` (flash: `-p COM32 flash`).
**[CAPTURE n]:** `& "...\idf5.5_py3.11_env\Scripts\python.exe" "...\firmware\build\cap.py" n`

**QMI8658:** addr `0x6b` on `power_bsp_i2c_bus()`. Regs: WHO_AM_I 0x00 (=0x05), CTRL1 0x02 (bit6 ADDR_AI), CTRL2 0x03 (accel FS/ODR), CTRL7 0x08 (bit0 aEN), Accel `0x35..0x3A` (6 bytes, LE int16 X,Y,Z).

---

## Task 1: `core/orient` — orientation decision (host-TDD)

**Files:** Create `core/include/rsvp/orient.hpp`, `core/src/orient.cpp`, `test/host/test_orient.cpp`; Modify `core/CMakeLists.txt`, `test/host/CMakeLists.txt`.

- [ ] **Step 1: Header** — `core/include/rsvp/orient.hpp`:
```cpp
#pragma once
namespace rsvp {

enum ScreenOrient { ORIENT_NORMAL, ORIENT_FLIPPED };

// +axis beyond +deadzone -> NORMAL; below -deadzone -> FLIPPED; within deadzone -> keep `current`.
ScreenOrient orientFromAxis(int axis, int deadzone, ScreenOrient current);

} // namespace rsvp
```

- [ ] **Step 2: Failing test** — `test/host/test_orient.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/orient.hpp"
using namespace rsvp;

TEST_CASE("orientFromAxis: beyond deadzone picks by sign") {
    CHECK(orientFromAxis(10000, 4000, ORIENT_FLIPPED) == ORIENT_NORMAL);
    CHECK(orientFromAxis(-10000, 4000, ORIENT_NORMAL) == ORIENT_FLIPPED);
    CHECK(orientFromAxis(4001, 4000, ORIENT_FLIPPED) == ORIENT_NORMAL);
    CHECK(orientFromAxis(-4001, 4000, ORIENT_NORMAL) == ORIENT_FLIPPED);
}

TEST_CASE("orientFromAxis: within deadzone keeps current") {
    CHECK(orientFromAxis(0, 4000, ORIENT_NORMAL) == ORIENT_NORMAL);
    CHECK(orientFromAxis(0, 4000, ORIENT_FLIPPED) == ORIENT_FLIPPED);
    CHECK(orientFromAxis(3999, 4000, ORIENT_FLIPPED) == ORIENT_FLIPPED);
    CHECK(orientFromAxis(-3999, 4000, ORIENT_NORMAL) == ORIENT_NORMAL);
}
```

- [ ] **Step 3: Register sources** — in `core/CMakeLists.txt` add `"src/orient.cpp"` to `SRCS`; in `test/host/CMakeLists.txt` add `test_orient.cpp` (test list) and `${CORE_DIR}/src/orient.cpp` (core-source list).

- [ ] **Step 4: Run, expect red** — [HOST]: link error (`orientFromAxis` undefined).

- [ ] **Step 5: Implement** — `core/src/orient.cpp`:
```cpp
#include "rsvp/orient.hpp"
namespace rsvp {

ScreenOrient orientFromAxis(int axis, int deadzone, ScreenOrient current) {
    if (axis >  deadzone) return ORIENT_NORMAL;
    if (axis < -deadzone) return ORIENT_FLIPPED;
    return current;
}

} // namespace rsvp
```

- [ ] **Step 6: Run, expect green** — [HOST]: all pass.

- [ ] **Step 7: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add core/include/rsvp/orient.hpp core/src/orient.cpp test/host/test_orient.cpp core/CMakeLists.txt test/host/CMakeLists.txt; git commit -m "feat(core): orientFromAxis decision with deadzone"
```

---

## Task 2: `imu_bsp` — QMI8658 driver + axis calibration

**Files:** Create `firmware/components/imu_bsp/imu_bsp.hpp`, `firmware/components/imu_bsp/imu_bsp.cpp`, `firmware/components/imu_bsp/CMakeLists.txt`; Modify `firmware/main/ui_menu.cpp`.

- [ ] **Step 1: Header** — `firmware/components/imu_bsp/imu_bsp.hpp`:
```cpp
#pragma once
#include <cstdint>

void imu_init();                                            // add 0x6b + enable accelerometer
bool imu_read_accel(int16_t& ax, int16_t& ay, int16_t& az); // raw counts; false on I2C error
```

- [ ] **Step 2: Implementation** — `firmware/components/imu_bsp/imu_bsp.cpp`:
```cpp
#include "imu_bsp.hpp"
#include "power_bsp.h"             // power_bsp_i2c_bus() -> I2C0
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char* TAG = "imu_bsp";
static const int   kTimeoutMs = 1000;
#define QMI8658_ADDR 0x6b
static i2c_master_dev_handle_t s_imu = NULL;

static void wr(uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    i2c_master_transmit(s_imu, b, 2, kTimeoutMs);
}

void imu_init() {
    if (s_imu) return;
    i2c_master_bus_handle_t bus = power_bsp_i2c_bus();
    if (!bus) { ESP_LOGW(TAG, "I2C0 bus not ready"); return; }
    i2c_device_config_t dev = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                .device_address = QMI8658_ADDR, .scl_speed_hz = 100000 };
    if (i2c_master_bus_add_device(bus, &dev, &s_imu) != ESP_OK) {
        ESP_LOGW(TAG, "add QMI8658 failed"); s_imu = NULL; return;
    }
    wr(0x02, 0x40);   // CTRL1: ADDR_AI (auto-increment) for burst reads
    wr(0x03, 0x04);   // CTRL2: accel +-2g, mid ODR
    wr(0x08, 0x01);   // CTRL7: enable accelerometer
    ESP_LOGI(TAG, "QMI8658 ready");
}

bool imu_read_accel(int16_t& ax, int16_t& ay, int16_t& az) {
    if (!s_imu) return false;
    uint8_t reg = 0x35, b[6];
    if (i2c_master_transmit_receive(s_imu, &reg, 1, b, 6, kTimeoutMs) != ESP_OK) return false;
    ax = (int16_t)(b[0] | (b[1] << 8));
    ay = (int16_t)(b[2] | (b[3] << 8));
    az = (int16_t)(b[4] | (b[5] << 8));
    return true;
}
```

- [ ] **Step 3: Component CMake** — `firmware/components/imu_bsp/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "imu_bsp.cpp"
    INCLUDE_DIRS "."
    REQUIRES power_bsp esp_driver_i2c)
```

- [ ] **Step 4: Temp calibration log** — in `firmware/main/ui_menu.cpp`, add `#include "imu_bsp.hpp"`, and in `ui_menu_init` after `rtc_init();` add:
```cpp
    imu_init();
    lv_timer_create([](lv_timer_t*){ int16_t x,y,z; if (imu_read_accel(x,y,z))   // TEMP calib
        ESP_LOGI("imudbg", "ax=%d ay=%d az=%d", x, y, z); }, 500, nullptr);
```

- [ ] **Step 5: Build + flash + calibrate** — **[BUILD]**, **[FLASH]**, **[CAPTURE 12]** while: hold the device in **normal** reading orientation (note `imudbg` values), then **flip it end-over-end 180°** (note the values). Identify the axis (`ax` or `ay`) whose **sign flips** between the two, and which sign is the normal orientation. Sanity: at rest, the down-axis magnitude should be ~16000 (±2g, 1 g). Record: `IN_SCREEN_AXIS` = ax or ay, and whether to negate it so **+value = normal**. (Used in Task 3.) Then remove the temp `lv_timer_create([...])` line (keep `imu_init();`).

- [ ] **Step 6: Commit**
```powershell
git add firmware/components/imu_bsp firmware/main/ui_menu.cpp; git commit -m "feat(firmware): imu_bsp QMI8658 accelerometer on the I2C0 bus"
```

---

## Task 3: Auto-rotate poll + Settings toggle

**Files:** Create `firmware/main/autorotate.h`, `firmware/main/autorotate.cpp`; Modify `firmware/main/CMakeLists.txt`, `firmware/main/app_settings.h`, `firmware/main/app_settings.cpp`, `firmware/main/ui_menu.cpp`.

- [ ] **Step 1: Setting** — in `firmware/main/app_settings.h` add to `Settings`: `bool auto_rotate = true;`. In `app_settings.cpp` `settings_load`, after the `paused` line add `if (nvs_get_u8(h, "rot", &b) == ESP_OK) s_settings.auto_rotate = b;`; in `settings_save`, after the `paused` line add `nvs_set_u8(h, "rot", s_settings.auto_rotate);`.

- [ ] **Step 2: Header** — `firmware/main/autorotate.h`:
```cpp
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void autorotate_start(void);   // start the IMU orientation timer (call once at boot, under LVGL lock)
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: Poll** — `firmware/main/autorotate.cpp`:
```cpp
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
    int axis = ay;   // CALIBRATION (Task 2): the in-screen axis that flips; negate if needed
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
```
(Set `int axis = ay;` to the axis/sign found in Task 2 — e.g. `int axis = -ax;`.)

- [ ] **Step 4: Register + wire** — add `"autorotate.cpp"` to `SRCS` in `firmware/main/CMakeLists.txt`. In `ui_menu.cpp` add `#include "autorotate.h"`, and in `ui_menu_init` after `imu_init();` add `autorotate_start();`.

- [ ] **Step 5: Settings switch** — in `ui_menu.cpp` `show_settings()`, after the `add_switch("Start paused", 2, settings().start_paused);` line add:
```cpp
    add_switch("Auto-rotate", 3, settings().auto_rotate);
```
and in `switch_cb`, change the final `else` to handle the new id:
```cpp
    else if (which == 2) { settings().start_paused = on; settings_save(); }
    else                 { settings().auto_rotate = on; settings_save(); }
```

- [ ] **Step 6: Build + flash + verify** — **[BUILD]**, **[FLASH]**. **Flip the device 180°** → after ~0.6 s the whole UI rotates so text stays upright; flip back → rotates back. **Lying flat** on a table → no flip. **Small jitters** → no flip. **Settings → Auto-rotate off** → flipping no longer rotates (locked); reboot → still off. **Taps land correctly in both orientations** (open a book, tap to pause; open the menu flipped, tap a tile) — if a tap is mirrored when flipped, add to `TouchInputReadCallback` (main.c): when `lv_display_get_rotation(lv_display_get_default()) == LV_DISPLAY_ROTATION_270`, set `lx = 639 - lx; ly = 171 - ly;` before feeding LVGL.

- [ ] **Step 7: Commit + notes**
```powershell
git add firmware/main/autorotate.h firmware/main/autorotate.cpp firmware/main/CMakeLists.txt firmware/main/app_settings.h firmware/main/app_settings.cpp firmware/main/ui_menu.cpp; git commit -m "feat(firmware): IMU auto-rotate (180 flip) + Settings toggle"
```
Then add an IMU/auto-rotate bullet to `docs/firmware-notes.md` (QMI8658 0x6b on I2C0; 180° flip via display rotation 90↔270; deadzone+hold; Auto-rotate setting) and commit `docs: IMU auto-rotate notes`.

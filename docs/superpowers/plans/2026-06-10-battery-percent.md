# Real Battery % Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show the real battery percentage next to the clock in the reader.

**Architecture:** A piecewise LiPo voltage→% curve in host-tested `core/battery`. A C `battery_bsp` reads GPIO4/ADC1-ch3 (÷3 divider, curve-fitting calibration). The reader shows the % on the existing clock timer.

**Tech Stack:** ESP-IDF 5.5.2 (`esp_adc`), LVGL 9, C++17 `core` (doctest).

---

## Conventions

**[HOST]:** `cmake --build "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\build\host" --config Debug` then `& "...\build\host\Debug\rsvp_tests.exe"`.
**[BUILD]/[FLASH]:** `$idfp="C:\Users\WilliamHerr\esp\v5.5.2\esp-idf"; $env:IDF_PATH=$idfp; $env:IDF_PYTHON_ENV_PATH="C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env"; & "$idfp\export.ps1" *> $null; idf.py -C "...\firmware" build` (flash: `-p COM32 flash`).
**[CAPTURE n]:** `& "...\idf5.5_py3.11_env\Scripts\python.exe" "...\firmware\build\cap.py" n`

**Battery ADC:** GPIO4 = `ADC_UNIT_1` / `ADC_CHANNEL_3`, `ADC_ATTEN_DB_12`, 12-bit, curve-fitting cali; `battery_mV = adc_pin_mV × 3` (÷3 divider).

---

## Task 1: `core/battery` — voltage→% curve (host-TDD)

**Files:** Create `core/include/rsvp/battery.hpp`, `core/src/battery.cpp`, `test/host/test_battery.cpp`; Modify `core/CMakeLists.txt`, `test/host/CMakeLists.txt`.

- [ ] **Step 1: Header** — `core/include/rsvp/battery.hpp`:
```cpp
#pragma once
namespace rsvp {

int batteryPercent(int mv);   // battery millivolts -> 0..100 via a piecewise LiPo curve

} // namespace rsvp
```

- [ ] **Step 2: Failing test** — `test/host/test_battery.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/battery.hpp"
using namespace rsvp;

TEST_CASE("batteryPercent clamps the ends") {
    CHECK(batteryPercent(3200) == 0);
    CHECK(batteryPercent(3300) == 0);
    CHECK(batteryPercent(4200) == 100);
    CHECK(batteryPercent(4300) == 100);
}

TEST_CASE("batteryPercent hits the curve points") {
    CHECK(batteryPercent(3700) == 25);
    CHECK(batteryPercent(3900) == 58);
    CHECK(batteryPercent(4000) == 76);
    CHECK(batteryPercent(3600) == 16);   // interpolated between 3500(8) and 3700(25)
}

TEST_CASE("batteryPercent is monotonic non-decreasing") {
    int prev = -1;
    for (int mv = 3200; mv <= 4300; mv += 25) {
        int p = batteryPercent(mv);
        CHECK(p >= prev);
        prev = p;
    }
}
```

- [ ] **Step 3: Register sources** — in `core/CMakeLists.txt` add `"src/battery.cpp"` to `SRCS`; in `test/host/CMakeLists.txt` add `test_battery.cpp` (test list) and `${CORE_DIR}/src/battery.cpp` (core-source list).

- [ ] **Step 4: Run, expect red** — [HOST]: link error (`batteryPercent` undefined).

- [ ] **Step 5: Implement** — `core/src/battery.cpp`:
```cpp
#include "rsvp/battery.hpp"
namespace rsvp {

int batteryPercent(int mv) {
    static const struct { int mv; int pct; } pts[] = {
        {3300, 0}, {3500, 8}, {3700, 25}, {3800, 40},
        {3900, 58}, {4000, 76}, {4100, 90}, {4200, 100},
    };
    const int n = (int)(sizeof(pts) / sizeof(pts[0]));
    if (mv <= pts[0].mv)     return 0;
    if (mv >= pts[n-1].mv)   return 100;
    for (int i = 1; i < n; i++) {
        if (mv < pts[i].mv) {
            const int lo_mv = pts[i-1].mv, hi_mv = pts[i].mv;
            const int lo_p  = pts[i-1].pct, hi_p = pts[i].pct;
            return lo_p + (mv - lo_mv) * (hi_p - lo_p) / (hi_mv - lo_mv);
        }
    }
    return 100;
}

} // namespace rsvp
```

- [ ] **Step 6: Run, expect green** — [HOST]: all pass.

- [ ] **Step 7: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add core/include/rsvp/battery.hpp core/src/battery.cpp test/host/test_battery.cpp core/CMakeLists.txt test/host/CMakeLists.txt; git commit -m "feat(core): batteryPercent LiPo voltage->percent curve"
```

---

## Task 2: `battery_bsp` — ADC driver

**Files:** Create `firmware/components/battery_bsp/battery_bsp.h`, `firmware/components/battery_bsp/battery_bsp.c`, `firmware/components/battery_bsp/CMakeLists.txt`; Modify `firmware/main/ui_menu.cpp`.

- [ ] **Step 1: Header** — `firmware/components/battery_bsp/battery_bsp.h`:
```c
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void batt_init(void);     // ADC1 oneshot ch3 + 12dB + curve-fitting cali
int  batt_read_mv(void);  // average ~16 reads -> calibrated mV x3 (battery mV); 0 on failure
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Implementation** — `firmware/components/battery_bsp/battery_bsp.c`:
```c
#include "battery_bsp.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char* TAG = "battery_bsp";
static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;

void batt_init(void) {
    if (s_adc) return;
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) { s_adc = NULL; return; }
    adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    adc_oneshot_config_channel(s_adc, ADC_CHANNEL_3, &ccfg);
    adc_cali_curve_fitting_config_t cal = { .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_3,
                                            .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) { s_cali = NULL; }
    ESP_LOGI(TAG, "battery ADC ready (cali=%d)", s_cali != NULL);
}

int batt_read_mv(void) {
    if (!s_adc || !s_cali) return 0;
    long sum = 0; int ok = 0;
    for (int i = 0; i < 16; i++) {
        int raw = 0, mv = 0;
        if (adc_oneshot_read(s_adc, ADC_CHANNEL_3, &raw) != ESP_OK) continue;
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) continue;
        sum += mv; ok++;
    }
    if (ok == 0) return 0;
    return (int)((sum / ok) * 3);   // /3 divider -> battery mV
}
```
(If the build errors on `.chan` not being a member of `adc_cali_curve_fitting_config_t`, remove that one initializer field — the curve-fitting scheme on the S3 keys off unit+atten.)

- [ ] **Step 3: Component CMake** — `firmware/components/battery_bsp/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "battery_bsp.c"
    INCLUDE_DIRS "."
    REQUIRES esp_adc)
```

- [ ] **Step 4: Temp boot check** — in `firmware/main/ui_menu.cpp` add `#include "battery_bsp.h"`, and in `ui_menu_init` after `imu_init();` (before `autorotate_start();`) add:
```cpp
    batt_init();
    ESP_LOGI("battdbg", "batt_mv=%d", batt_read_mv());   // TEMP
```
**[BUILD]**, **[FLASH]**, **[CAPTURE 6]** → expect `battery_bsp: battery ADC ready` and a `battdbg: batt_mv=<n>` with a plausible value (USB-powered ≈ 4100–4300; on battery 3300–4200). Then remove the temp `ESP_LOGI("battdbg"...)` line (keep `batt_init();`).

- [ ] **Step 5: Commit**
```powershell
git add firmware/components/battery_bsp firmware/main/ui_menu.cpp; git commit -m "feat(firmware): battery_bsp reads the battery ADC (GPIO4/ADC1-ch3)"
```

---

## Task 3: Battery % in the reader

**Files:** Modify `firmware/main/ui_reader.cpp`.

- [ ] **Step 1: Includes + globals** — in `ui_reader.cpp` add `#include "battery_bsp.h"` and `#include "rsvp/battery.hpp"`. Next to `g_clk` (in the anon namespace) add:
```cpp
lv_obj_t* g_batt = nullptr;     // battery % label (in the top-right cluster with the clock)

void batt_update() {
    if (g_batt == nullptr) return;
    int mv = batt_read_mv();
    if (mv > 0) { char b[8]; std::snprintf(b, sizeof b, "%d%%", batteryPercent(mv)); lv_label_set_text(g_batt, b); }
    else        lv_label_set_text(g_batt, "");
}
```
And in `clock_timer_cb`, call `batt_update();` alongside `clock_update();`:
```cpp
void clock_timer_cb(lv_timer_t*) { clock_update(); batt_update(); }
```

- [ ] **Step 2: Build the top-right cluster** — in `build_reader`, replace the current clock-label block:
```cpp
    g_clk = make_label(g_scr, dim, &lv_font_montserrat_16);
    lv_obj_align(g_clk, LV_ALIGN_TOP_RIGHT, -10, 6);
    clock_update();                 // show the time immediately
```
with a flex cluster holding the battery % (left) and the clock (right):
```cpp
    lv_obj_t* topr = lv_obj_create(g_scr);
    lv_obj_remove_style_all(topr);
    lv_obj_set_size(topr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(topr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topr, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(topr, 10, 0);
    lv_obj_align(topr, LV_ALIGN_TOP_RIGHT, -10, 6);
    g_batt = make_label(topr, dim, &lv_font_montserrat_16);
    g_clk  = make_label(topr, dim, &lv_font_montserrat_16);
    clock_update();                 // show time + battery immediately
    batt_update();
```
And at the top of `build_reader` where `g_clk = nullptr;` is set (before `lv_obj_clean`), add `g_batt = nullptr;` next to it.

- [ ] **Step 3: Build + flash + verify** — **[BUILD]**, **[FLASH]**. Open a book → the top-right shows `<NN>%   <time>` (battery left of the clock). On USB it reads near 100%. The title (left) and word display are unaffected. Flip the device (auto-rotate) → the cluster rotates with everything.

- [ ] **Step 4: Commit + notes**
```powershell
git add firmware/main/ui_reader.cpp; git commit -m "feat(firmware): show battery % next to the clock"
```
Then add a battery bullet to `docs/firmware-notes.md` (GPIO4/ADC1-ch3, ÷3 divider, 12dB + curve-fitting cali, piecewise LiPo % in `core/battery`) and commit `docs: battery % notes`.

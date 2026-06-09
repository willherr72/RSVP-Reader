# RSVP Touch Input Reliability & Gesture Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make touch reliable on the ESP32-S3-Touch-LCD-3.49 and wire RSVP controls (tap=play/pause, swipe ↑/↓=WPM, swipe ←/→=sentence).

**Architecture:** Root cause is poll starvation on LVGL's single render thread (confirmed: 8–15 Hz under continuous full-frame redraws). Fix in priority order: (1) cut render load so LVGL processes input ≥30 Hz, (2) swap the hand-rolled I2C poll for the maintained `esp_lcd_touch` driver and fix the indev↔display rotation mismatch, (3) decouple sampling into a ~100 Hz task + raise the indev rate, (4) classify gestures with a host-tested pure function and dispatch to `Player`. We measure on-device after each firmware change (board on COM32) and strip the instrumentation last.

**Tech Stack:** ESP-IDF 5.5.2, LVGL 9, `espressif/esp_lcd_axs15231b ^1.0.1` (panel + `esp_lcd_touch` driver), C++17 `core/` engine, doctest host tests.

---

## Conventions (referenced by tasks)

**[BUILD]** — build the firmware:
```powershell
$idf = "C:\Users\WilliamHerr\esp\v5.5.2\esp-idf"; $env:IDF_PATH = $idf; $env:IDF_PYTHON_ENV_PATH = "C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env"; & "$idf\export.ps1" *> $null; idf.py -C "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\firmware" build
```

**[FLASH]** — same env prefix, then: `idf.py -C "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\firmware" -p COM32 flash`

**[MEASURE]** — capture 30 s of serial while the user taps ~1/s (run in background, then read the task output):
```powershell
& "C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe" "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\firmware\build\touch_capture.py"
```

**[HOST-TEST]** — build + run host tests:
```powershell
cmake --build "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\build\host" --config Debug
& "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\build\host\Debug\rsvp_tests.exe"
```
(If `build/host` is not configured yet: `cmake -S "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\test\host" -B "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\build\host" -G "Visual Studio 17 2022"`.)

---

## Task 1: `classifyGesture` pure function (host-tested, no hardware)

**Files:**
- Create: `core/include/rsvp/gesture.hpp`
- Create: `core/src/gesture.cpp`
- Test: `test/host/test_gesture.cpp`
- Modify: `test/host/CMakeLists.txt`, `core/CMakeLists.txt`

- [ ] **Step 1: Write the header and a stub implementation**

`core/include/rsvp/gesture.hpp`:
```cpp
#pragma once

namespace rsvp {

enum class Gesture { None, Tap, SwipeUp, SwipeDown, SwipeLeft, SwipeRight };

// dx,dy = release-minus-press in LVGL screen pixels (y grows downward).
// |delta| within tapRadius on both axes -> Tap; otherwise the dominant axis
// sets the swipe direction (ties resolve horizontal). Pure/deterministic.
// Always returns Tap or a Swipe*; None is reserved for the caller.
Gesture classifyGesture(int dx, int dy, int tapRadius);

} // namespace rsvp
```

`core/src/gesture.cpp` (stub — real logic lands in Step 5, so the test goes red on behavior, not a missing-file/link error):
```cpp
#include "rsvp/gesture.hpp"

namespace rsvp {

Gesture classifyGesture(int /*dx*/, int /*dy*/, int /*tapRadius*/) {
    return Gesture::None;
}

} // namespace rsvp
```

- [ ] **Step 2: Write the failing test**

`test/host/test_gesture.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/gesture.hpp"
using namespace rsvp;

TEST_CASE("classifyGesture: small movement is a tap") {
    CHECK(classifyGesture(0, 0, 20)    == Gesture::Tap);
    CHECK(classifyGesture(20, 0, 20)   == Gesture::Tap);   // on the radius
    CHECK(classifyGesture(-15, 19, 20) == Gesture::Tap);
}

TEST_CASE("classifyGesture: dominant horizontal axis -> left/right") {
    CHECK(classifyGesture(40, 5, 20)   == Gesture::SwipeRight);
    CHECK(classifyGesture(-40, -5, 20) == Gesture::SwipeLeft);
}

TEST_CASE("classifyGesture: dominant vertical axis -> up/down") {
    CHECK(classifyGesture(5, -40, 20)  == Gesture::SwipeUp);    // up = negative dy
    CHECK(classifyGesture(-5, 40, 20)  == Gesture::SwipeDown);
}

TEST_CASE("classifyGesture: axis tie resolves horizontal") {
    CHECK(classifyGesture(30, 30, 20)   == Gesture::SwipeRight);
    CHECK(classifyGesture(-30, -30, 20) == Gesture::SwipeLeft);
}

TEST_CASE("classifyGesture: just past the radius on one axis swipes") {
    CHECK(classifyGesture(21, 0, 20) == Gesture::SwipeRight);
    CHECK(classifyGesture(0, 21, 20) == Gesture::SwipeDown);
}
```

- [ ] **Step 3: Register the new files in both builds**

In `test/host/CMakeLists.txt`, add `test_gesture.cpp` to the `add_executable(rsvp_tests ...)` list (after `test_orp.cpp`) and add `${CORE_DIR}/src/gesture.cpp` to the source list (after `${CORE_DIR}/src/orp.cpp`).

In `core/CMakeLists.txt`, add `"src/gesture.cpp"` to the `SRCS` list (after `"src/orp.cpp"`).

- [ ] **Step 4: Run host tests to verify the new cases fail on behavior**

Run **[HOST-TEST]**. Expected: build SUCCEEDS, but the `classifyGesture` cases FAIL (the stub returns `None`, so every `CHECK` mismatches).

- [ ] **Step 5: Replace the stub with the real implementation**

Overwrite `core/src/gesture.cpp`:
```cpp
#include "rsvp/gesture.hpp"
#include <cstdlib>

namespace rsvp {

Gesture classifyGesture(int dx, int dy, int tapRadius) {
    const int ax = std::abs(dx);
    const int ay = std::abs(dy);
    if (ax <= tapRadius && ay <= tapRadius) return Gesture::Tap;
    if (ax >= ay) return dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
    return dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
}

} // namespace rsvp
```

- [ ] **Step 6: Run host tests to verify pass**

Run **[HOST-TEST]**. Expected: all cases PASS (count is the prior total + 5 new cases).

- [ ] **Step 7: Commit**

```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add core/include/rsvp/gesture.hpp core/src/gesture.cpp test/host/test_gesture.cpp test/host/CMakeLists.txt core/CMakeLists.txt; git commit -m "feat(core): host-tested classifyGesture for tap/swipe"
```

---

## Task 2: Cut render load (the load-bearing fix) + measure

**Files:**
- Modify: `firmware/sdkconfig.defaults`
- Modify: `firmware/main/ui_reader.cpp` (`tick_cb`)
- Modify: `firmware/main/main.c` (simplify the instrumentation log — drop the now-defunct `redraw=` toggle field)

- [ ] **Step 1: Disable the LVGL FPS overlay**

In `firmware/sdkconfig.defaults`, replace the perf-monitor lines. Change:
```
CONFIG_LV_USE_SYSMON=y
CONFIG_LV_USE_PERF_MONITOR=y
```
to:
```
CONFIG_LV_USE_SYSMON=n
CONFIG_LV_USE_PERF_MONITOR=n
```
(Leaving the `LV_USE_DEMO_*` flags as-is — they don't draw unless invoked; out of scope.)

- [ ] **Step 2: Remove the 30 fps debug redraw from `tick_cb`**

In `firmware/main/ui_reader.cpp`, replace the STARVE TEST block at the end of `tick_cb` (the `const uint32_t sec = …` through the closing `}` of the `if (redraw_phase)`) so the function ends right after the word-change refresh:
```cpp
    if (g_player->index() != g_lastIdx) {
        g_lastIdx = g_player->index();
        refresh_word();
    }
}
```
(`refresh_word()` already calls `update_status()`, so the `wpm · %` line stays current without a per-tick redraw. Leave the `esp_timer.h` include — still used elsewhere? If not, leave it; harmless.)

- [ ] **Step 3: Simplify the read-rate log in `main.c`**

In `firmware/main/main.c` `TouchInputReadCallback`, the per-second log still references the removed toggle. Replace:
```cpp
    if (now_us - s_touch_log_us >= 1000000) {
        bool redraw_phase = (((uint32_t)(now_us / 1000000)) / 5) % 2 == 0;
        ESP_LOGI(TAG, "touch reads/s=%u presses/s=%u redraw=%d",
                 (unsigned)s_touch_reads, (unsigned)s_touch_presses, (int)redraw_phase);
        s_touch_reads = 0;
        s_touch_presses = 0;
        s_touch_log_us = now_us;
    }
```
with:
```cpp
    if (now_us - s_touch_log_us >= 1000000) {
        ESP_LOGI(TAG, "touch reads/s=%u presses/s=%u",
                 (unsigned)s_touch_reads, (unsigned)s_touch_presses);
        s_touch_reads = 0;
        s_touch_presses = 0;
        s_touch_log_us = now_us;
    }
```

- [ ] **Step 4: Build, flash, measure**

Run **[BUILD]** (expect `Project build complete`), then **[FLASH]**, then **[MEASURE]** while the user taps ~1/s.

- [ ] **Step 5: Verify the rate improved**

Expected in the capture: steady-state `reads/s` now **≥ 30** (up from the 8–15 Hz baseline) and `evt PRESSED`/`evt RELEASED` pairs appear ~1:1 per tap with in-bounds deltas. If still < 30 Hz, do not proceed — the FULL-mode flush itself is the limiter; note it and revisit render mode before Task 3.

- [ ] **Step 6: Commit**

```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add firmware/sdkconfig.defaults firmware/main/ui_reader.cpp firmware/main/main.c; git commit -m "fix(firmware): stop continuous full-frame redraws starving touch poll"
```

---

## Task 3: Swap to the `esp_lcd_touch` driver + fix coordinate/rotation + measure

**Files:**
- Modify: `firmware/main/idf_component.yml` (declare `esp_lcd_touch` explicitly)
- Modify: `firmware/main/main.c` (includes, touch handle creation, rewrite `TouchInputReadCallback`)

- [ ] **Step 1: Declare the touch dependency explicitly**

In `firmware/main/idf_component.yml`, under `dependencies:`, add:
```yaml
  espressif/esp_lcd_touch: "^1.1"
```
(It is already pulled transitively by `esp_lcd_axs15231b`; declaring it makes the headers' availability explicit.)

- [ ] **Step 2: Add includes + a touch handle in `main.c`**

In `firmware/main/main.c`, after `#include "esp_lcd_axs15231b.h"` (line ~18) add:
```c
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_axs15231b.h"
```
Below the `s_touch_*` debug statics, add:
```c
static esp_lcd_touch_handle_t g_tp = NULL;
```

- [ ] **Step 3: Create the touch handle after the indev is created**

In `main.c`, immediately after `lv_indev_set_read_cb(touch_indev, TouchInputReadCallback);` (line ~322), insert:
```c
    /* AXS15231B touch over the shared I2C bus (user_i2c_port1_handle). */
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(user_i2c_port1_handle, &tp_io_cfg, &tp_io));
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,   // LVGL logical (pre-rotation) width
        .y_max = EXAMPLE_LCD_V_RES,   // LVGL logical (pre-rotation) height
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .flags = { .swap_xy = 1, .mirror_x = 0, .mirror_y = 0 },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_axs15231b(tp_io, &tp_cfg, &g_tp));
```
Rationale: raw touch is physical-landscape (X∈[0,640], Y∈[0,172]); LVGL's logical space is portrait `EXAMPLE_LCD_H_RES × EXAMPLE_LCD_V_RES` (172×640) which `lv_display_set_rotation(90)` maps to the panel. `swap_xy=1` converts landscape→portrait; `mirror_*` get dialed in by the corner-tap check in Step 6.

- [ ] **Step 4: Rewrite `TouchInputReadCallback` to use the driver**

Replace the whole body of `TouchInputReadCallback` (from `uint8_t read_touchpad_cmd[...]` through the final `else { … RELEASED … }`) with:
```c
    uint16_t x = 0, y = 0;
    uint8_t  cnt = 0;
    esp_lcd_touch_read_data(g_tp);
    bool pressed_now = esp_lcd_touch_get_coordinates(g_tp, &x, &y, NULL, &cnt, 1) && cnt > 0;

    // DEBUG instrumentation (kept until Task 6).
    s_touch_reads++;
    if (pressed_now) s_touch_presses++;
    if (pressed_now && !s_touch_was_pressed) {
        ESP_LOGI(TAG, "touch PRESS edge x=%u y=%u", (unsigned)x, (unsigned)y);
    }
    s_touch_was_pressed = pressed_now;
    int64_t now_us = esp_timer_get_time();
    if (now_us - s_touch_log_us >= 1000000) {
        ESP_LOGI(TAG, "touch reads/s=%u presses/s=%u",
                 (unsigned)s_touch_reads, (unsigned)s_touch_presses);
        s_touch_reads = 0;
        s_touch_presses = 0;
        s_touch_log_us = now_us;
    }

    if (pressed_now) {
        indevData->state = LV_INDEV_STATE_PRESSED;
        indevData->point.x = x;
        indevData->point.y = y;
    } else {
        indevData->state = LV_INDEV_STATE_RELEASED;
    }
```

- [ ] **Step 5: Build, flash, measure**

Run **[BUILD]** (managed-component fetch may re-run), **[FLASH]**, **[MEASURE]** while the user taps the **four corners** then the center.

- [ ] **Step 6: Verify clean events + correct coordinates**

Expected: clean 1:1 `PRESSED`/`RELEASED`, deltas in-bounds. From the corner taps, confirm the logged `evt PRESSED` point matches the physical corner: top-left ≈ small x & small y, bottom-right ≈ large x & large y. If a corner is mirrored, flip `mirror_x` and/or `mirror_y` in Step 3 and re-run Steps 5–6 until corners map correctly.

- [ ] **Step 7: Commit**

```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add firmware/main/main.c firmware/main/idf_component.yml; git commit -m "feat(firmware): drive touch via esp_lcd_touch AXS15231B + fix indev rotation"
```

---

## Task 4: Decouple sampling into a ~100 Hz task + raise the indev rate + measure

**Files:**
- Modify: `firmware/main/main.c` (snapshot + spinlock, sampling task, thin `read_cb`, indev period)

- [ ] **Step 1: Add a lock-guarded touch snapshot**

In `main.c`, below `static esp_lcd_touch_handle_t g_tp = NULL;` add:
```c
static portMUX_TYPE g_touch_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool     g_touch_pressed = false;
static volatile uint16_t g_touch_x = 0;
static volatile uint16_t g_touch_y = 0;

static void touch_sample_task(void *arg)
{
    for (;;) {
        uint16_t x = 0, y = 0;
        uint8_t  cnt = 0;
        esp_lcd_touch_read_data(g_tp);
        bool pressed = esp_lcd_touch_get_coordinates(g_tp, &x, &y, NULL, &cnt, 1) && cnt > 0;
        taskENTER_CRITICAL(&g_touch_mux);
        g_touch_pressed = pressed;
        if (pressed) { g_touch_x = x; g_touch_y = y; }
        taskEXIT_CRITICAL(&g_touch_mux);
        vTaskDelay(pdMS_TO_TICKS(10));   // ~100 Hz
    }
}
```

- [ ] **Step 2: Make `TouchInputReadCallback` a thin snapshot copy**

Replace the driver-read body added in Task 3 with a snapshot read (keep the instrumentation):
```c
    bool pressed_now;
    uint16_t x, y;
    taskENTER_CRITICAL(&g_touch_mux);
    pressed_now = g_touch_pressed;
    x = g_touch_x;
    y = g_touch_y;
    taskEXIT_CRITICAL(&g_touch_mux);

    // DEBUG instrumentation (kept until Task 6).
    s_touch_reads++;
    if (pressed_now) s_touch_presses++;
    if (pressed_now && !s_touch_was_pressed) {
        ESP_LOGI(TAG, "touch PRESS edge x=%u y=%u", (unsigned)x, (unsigned)y);
    }
    s_touch_was_pressed = pressed_now;
    int64_t now_us = esp_timer_get_time();
    if (now_us - s_touch_log_us >= 1000000) {
        ESP_LOGI(TAG, "touch reads/s=%u presses/s=%u",
                 (unsigned)s_touch_reads, (unsigned)s_touch_presses);
        s_touch_reads = 0; s_touch_presses = 0; s_touch_log_us = now_us;
    }

    if (pressed_now) {
        indevData->state = LV_INDEV_STATE_PRESSED;
        indevData->point.x = x;
        indevData->point.y = y;
    } else {
        indevData->state = LV_INDEV_STATE_RELEASED;
    }
```

- [ ] **Step 3: Start the sampling task and speed up the indev read timer**

In `main.c`, after the `esp_lcd_touch_new_i2c_axs15231b(...)` call, add:
```c
    lv_timer_set_period(lv_indev_get_read_timer(touch_indev), 16);  // ~60 Hz indev processing
    xTaskCreatePinnedToCore(touch_sample_task, "touch", 4 * 1024, NULL, 3, NULL, 1);
```
(Pinned to core 1, priority 3 — above the LVGL task so sampling stays regular.)

- [ ] **Step 4: Build, flash, measure**

Run **[BUILD]**, **[FLASH]**, **[MEASURE]** while the user taps ~2/s and does a few swipes.

- [ ] **Step 5: Verify**

Expected: `reads/s` ≈ 60 (indev now processes at the 16 ms period); every tap yields a clean `PRESSED`+`RELEASED`; swipe deltas are realistic (|dy| ≤ ~172, |dx| ≤ ~640). No missed taps across ~20.

- [ ] **Step 6: Commit**

```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add firmware/main/main.c; git commit -m "perf(firmware): sample touch in a 100Hz task; 60Hz indev, decoupled from render"
```

---

## Task 5: Wire gestures to Player controls + measure

**Files:**
- Modify: `firmware/main/ui_reader.cpp` (include `gesture.hpp`, rewrite `touch_event_cb`)

- [ ] **Step 1: Include the gesture classifier**

In `firmware/main/ui_reader.cpp`, add to the `rsvp/*` include block:
```cpp
#include "rsvp/gesture.hpp"
```

- [ ] **Step 2: Dispatch gestures on release**

Replace the body of `touch_event_cb` (the diagnostic version) with:
```cpp
    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr || g_player == nullptr) return;
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &g_press_pt);
        return;
    }
    if (code != LV_EVENT_RELEASED) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const int dx = static_cast<int>(p.x) - static_cast<int>(g_press_pt.x);
    const int dy = static_cast<int>(p.y) - static_cast<int>(g_press_pt.y);

    switch (classifyGesture(dx, dy, 25)) {
        case Gesture::Tap:        g_player->togglePlay(); update_status(); break;
        case Gesture::SwipeUp:    set_wpm(g_wpm + 25); break;   // up = faster
        case Gesture::SwipeDown:  set_wpm(g_wpm - 25); break;
        case Gesture::SwipeLeft:  do_prev_sentence(); break;
        case Gesture::SwipeRight: do_next_sentence(); break;
        case Gesture::None:       break;
    }
```
(Removes the use of `g_pc/g_rc/g_ldx/g_ldy`; those globals and the `tick_cb`/`main.c` instrumentation are stripped in Task 6.)

- [ ] **Step 3: Build, flash, manual on-device check**

Run **[BUILD]**, **[FLASH]**. Then verify by hand on the device:
- Tap → word stream pauses; tap again → resumes (pause glyph appears/disappears).
- Swipe up → WPM rises in the status line; swipe down → falls (clamped 100–800).
- Swipe left → jumps back a sentence; swipe right → forward.

- [ ] **Step 4: Verify the gesture map with the serial log**

Run **[MEASURE]** and perform one of each gesture; confirm each `evt RELEASED` delta corresponds to the action observed (e.g. large negative `dy` coincided with a WPM increase).

- [ ] **Step 5: Commit**

```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add firmware/main/ui_reader.cpp; git commit -m "feat(firmware): tap=play/pause, swipe=WPM/sentence via classifyGesture"
```

---

## Task 6: Strip instrumentation + final verification

**Files:**
- Modify: `firmware/main/main.c` (remove `s_touch_*` statics + all touch debug logging)
- Modify: `firmware/main/ui_reader.cpp` (remove `g_pc/g_rc/g_ldx/g_ldy`, the `esp_log.h`/`esp_timer.h` includes + `TAG` if now unused, and any debug logging in `touch_event_cb`)
- Delete: `firmware/build/touch_capture.py` (untracked; just remove the file)

- [ ] **Step 1: Remove the read-rate instrumentation from `main.c`**

Delete the `// --- DEBUG: touch-starvation diagnosis ---` block of statics (`s_touch_reads`, `s_touch_presses`, `s_touch_log_us`, `s_touch_was_pressed`) and, inside `TouchInputReadCallback`, delete the instrumentation lines (the `s_touch_*` updates, the `touch PRESS edge` log, and the per-second `reads/s` log), leaving only the snapshot copy and the LVGL state assignment.

- [ ] **Step 2: Remove debug globals/includes from `ui_reader.cpp`**

Delete `int g_pc = 0, g_rc = 0, g_ldx = 0, g_ldy = 0;` and the `const char *TAG = …`. Remove `#include "esp_log.h"`; keep `#include "esp_timer.h"` only if still referenced (it is not after Task 2 — remove it too). Ensure `touch_event_cb` no longer references any removed global.

- [ ] **Step 3: Build + host tests**

Run **[BUILD]** (expect clean `Project build complete`; the unused-function warnings for `set_wpm`/`do_*_sentence` should be gone now that they're called). Run **[HOST-TEST]** (expect all PASS).

- [ ] **Step 4: Final on-device acceptance**

Run **[FLASH]**. Confirm the definition of done: ~20 consecutive taps all toggle play/pause; vertical swipes step WPM (status tracks it); horizontal swipes jump sentence; controls feel immediate. No debug text on screen or serial.

- [ ] **Step 5: Remove the capture script and commit**

```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; Remove-Item "firmware\build\touch_capture.py" -ErrorAction SilentlyContinue; git add firmware/main/main.c firmware/main/ui_reader.cpp; git commit -m "chore(firmware): remove touch-starvation diagnostics after fix verified"
```

- [ ] **Step 6: Update the handoff notes**

In `docs/firmware-notes.md`, update the touch Status section: mark touch reliable, note the root cause (render-thread poll starvation), the `esp_lcd_touch` + 100 Hz sampling task + 60 Hz indev solution, and the gesture map. Commit:
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add docs/firmware-notes.md; git commit -m "docs: touch input reliable — root cause + gesture controls"
```
```

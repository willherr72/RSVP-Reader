# Power Button + Power-Hold Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep the board powered under battery (assert TCA9554 P6) and power it off via a ~1.5 s PWR long-press.

**Architecture:** A new firmware component `power_bsp` creates the I2C0 bus + TCA9554 expander and asserts **P6 HIGH** (power-hold) first thing in `app_main`; a poll task on GPIO16 detects a long-press and fires a callback; `ui_reader` shows a brief "Powering off…" screen and calls `power_off()` (P6 LOW). This is hardware glue (I2C/GPIO) — verified on-device, no host tests (like `sdcard_bsp`).

**Tech Stack:** ESP-IDF 5.5.2 (`esp_driver_i2c`, `esp_io_expander_tca9554`), LVGL 9, FreeRTOS.

---

## Conventions

**[BUILD]/[FLASH]**: `$idf="C:\Users\WilliamHerr\esp\v5.5.2\esp-idf"; $env:IDF_PATH=$idf; $env:IDF_PYTHON_ENV_PATH="C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env"; & "$idf\export.ps1" *> $null; idf.py -C "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\firmware" build` (flash: append `-p COM32 flash`).

**[CAPTURE n]**: `& "C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe" "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\firmware\build\cap.py" n`

**Facts:** touch I2C = `I2C_NUM_1` (so port 0 is free). TCA9554 on **I2C0 / GPIO48 SCL / GPIO47 SDA / addr 000**, **P6** = power-hold (HIGH=on, LOW=off). PWR button = **GPIO16**, active-low. On USB the board stays powered regardless of P6, so a power-off there just blanks the panel — expected.

---

## Task 1: `power_bsp` component — power-hold (P6) + power_off

**Files:** Create `firmware/components/power_bsp/power_bsp.h`, `firmware/components/power_bsp/power_bsp.c`, `firmware/components/power_bsp/CMakeLists.txt`, `firmware/components/power_bsp/idf_component.yml`; Modify `firmware/main/main.c`.

- [ ] **Step 1: Header** — `firmware/components/power_bsp/power_bsp.h`:
```c
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
```

- [ ] **Step 2: Implementation** — `firmware/components/power_bsp/power_bsp.c` (button task is a stub here; Task 2 fills it in):
```c
#include "power_bsp.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_io_expander_tca9554.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PWR_HOLD_PIN  IO_EXPANDER_PIN_NUM_6   // TCA9554 P6: HIGH=stay on, LOW=power off
#define PWR_BTN_GPIO  GPIO_NUM_16             // PWR button, active-low
#define I2C0_SCL_GPIO GPIO_NUM_48
#define I2C0_SDA_GPIO GPIO_NUM_47

static const char *TAG = "power_bsp";
static esp_io_expander_handle_t s_io = NULL;
static power_shutdown_cb_t s_shutdown_cb = NULL;

void power_bsp_set_shutdown_cb(power_shutdown_cb_t cb) { s_shutdown_cb = cb; }

void power_off(void)
{
    ESP_LOGI(TAG, "power_off: driving P6 low");
    if (s_io) esp_io_expander_set_level(s_io, PWR_HOLD_PIN, 0);
}

static void power_button_task(void *arg)
{
    (void)arg;
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));   // replaced in Task 2
}

void power_bsp_init(void)
{
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C0_SCL_GPIO,
        .sda_io_num = I2C0_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    if (i2c_new_master_bus(&cfg, &bus) != ESP_OK) {
        ESP_LOGW(TAG, "I2C0 bus init failed; power-hold unavailable");
        return;
    }
    if (esp_io_expander_new_i2c_tca9554(bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &s_io) != ESP_OK) {
        ESP_LOGW(TAG, "TCA9554 init failed; power-hold unavailable");
        s_io = NULL;
        return;
    }
    esp_io_expander_set_dir(s_io, PWR_HOLD_PIN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(s_io, PWR_HOLD_PIN, 1);   // hold power on
    ESP_LOGI(TAG, "power-hold asserted (TCA9554 P6 high)");

    gpio_config_t btn = {
        .pin_bit_mask = (uint64_t)1 << PWR_BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn);

    xTaskCreatePinnedToCore(power_button_task, "pwrbtn", 3 * 1024, NULL, 5, NULL, 1);
}
```

- [ ] **Step 3: Component CMake** — `firmware/components/power_bsp/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "power_bsp.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c esp_driver_gpio esp_io_expander esp_io_expander_tca9554)
```

- [ ] **Step 4: Managed dependency** — `firmware/components/power_bsp/idf_component.yml`:
```yaml
dependencies:
  espressif/esp_io_expander_tca9554: "^2"
```

- [ ] **Step 5: Call it first in `app_main`** — in `firmware/main/main.c` add `#include "power_bsp.h"` with the other includes, and make `power_bsp_init();` the **first** statement inside `app_main` (before `lcd_bl_pwm_bsp_init(...)`):
```c
void app_main(void)
{
    power_bsp_init();   // assert battery power-hold (TCA9554 P6) ASAP
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);
    // ... rest unchanged ...
```

- [ ] **Step 6: Build + flash + verify** — **[BUILD]**, **[FLASH]**, **[CAPTURE 8]**. Expected serial: `power_bsp: power-hold asserted (TCA9554 P6 high)`, then SD mounts and the reader loads as before. On-screen (USB): reader shows + stays lit (P6=1 keeps the panel up). **On battery (manual):** press PWR to turn on, release — the device now **stays on** and boots to the reader (previously it died).

- [ ] **Step 7: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add firmware/components/power_bsp firmware/main/main.c; git commit -m "feat(firmware): power_bsp asserts TCA9554 P6 power-hold at boot"
```

---

## Task 2: PWR button long-press detection

**Files:** Modify `firmware/components/power_bsp/power_bsp.c`.

- [ ] **Step 1: Replace the stub button task** — in `firmware/components/power_bsp/power_bsp.c` replace the whole `power_button_task` stub with the real detector:
```c
static void power_button_task(void *arg)
{
    (void)arg;
    const int kPollMs    = 20;
    const int kLongCount = 1500 / kPollMs;   // ~1.5 s of continuous press = 75 samples
    int  pressed = 0;
    bool released_seen = false;   // boot guard: require one release before arming
    bool fired = false;
    for (;;) {
        bool down = (gpio_get_level(PWR_BTN_GPIO) == 0);   // active-low
        if (!down) {
            released_seen = true;
            pressed = 0;
            fired = false;
        } else if (released_seen && !fired) {
            if (++pressed >= kLongCount) {
                fired = true;
                ESP_LOGI(TAG, "PWR long-press -> shutdown");
                if (s_shutdown_cb) s_shutdown_cb();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }
}
```

- [ ] **Step 2: Build + flash + verify** — **[BUILD]**, **[FLASH]**. With no shutdown callback registered yet, the board just logs. **[CAPTURE 15]** while you: (a) tap PWR briefly → no log; (b) hold PWR ~1.5 s → `power_bsp: PWR long-press -> shutdown` appears once; the board keeps running (no callback yet). Holding PWR through boot does NOT log until you release once (boot guard).

- [ ] **Step 3: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add firmware/components/power_bsp/power_bsp.c; git commit -m "feat(firmware): detect PWR long-press in power_bsp button task"
```

---

## Task 3: "Powering off…" UI + power_off wiring

**Files:** Modify `firmware/main/ui_reader.cpp`.

- [ ] **Step 1: Include the header** — in `firmware/main/ui_reader.cpp` add with the other includes:
```cpp
#include "power_bsp.h"
```

- [ ] **Step 2: Shutdown flag + callback + UI timer** — in `ui_reader.cpp`, inside the anonymous `namespace {` (near `g_load_done`), add:
```cpp
std::atomic<bool> g_shutdown_requested{false};

// Registered with power_bsp; runs in the button task -> only flip an atomic flag.
void on_shutdown_requested() { g_shutdown_requested.store(true); }

// LVGL thread: on shutdown, cover the screen with "Powering off...", render it, then cut power.
void shutdown_timer_cb(lv_timer_t* t)
{
    if (!g_shutdown_requested.load()) return;
    lv_obj_t* scr = lv_screen_active();
    lv_obj_t* o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(o, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_t* lbl = lv_label_create(o);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xf2f5fa), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
    lv_label_set_text(lbl, "Powering off...");
    lv_obj_center(lbl);
    lv_refr_now(NULL);                 // force the message to the panel before power cuts
    vTaskDelay(pdMS_TO_TICKS(600));    // let it show
    power_off();                       // P6 low -> battery powers down (USB: panel cuts)
    lv_timer_del(t);
}
```

- [ ] **Step 3: Register the callback + timer** — in `rsvp_loading_screen_create()` (in `ui_reader.cpp`), add these two lines immediately **before** the `if (xTaskCreatePinnedToCore(load_task, ...) ...)` line:
```cpp
    power_bsp_set_shutdown_cb(on_shutdown_requested);
    lv_timer_create(shutdown_timer_cb, 100, nullptr);
```
(`on_shutdown_requested` is a plain `void(void)`, ABI-compatible with `power_shutdown_cb_t`; GCC accepts it as the C callback.)

- [ ] **Step 4: Build + flash + verify** — **[BUILD]**, **[FLASH]**, then **[CAPTURE 15]** while holding PWR ~1.5 s. Expected serial: `PWR long-press -> shutdown` → (panel shows **"Powering off..."**) → `power_bsp: power_off: driving P6 low`. On **USB** the panel goes dark but serial keeps running (board stays alive — expected). On **battery (manual):** long-press → "Powering off..." → the device **powers down**. A short tap does nothing.

- [ ] **Step 5: Commit + update notes**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add firmware/main/ui_reader.cpp; git commit -m "feat(firmware): Powering-off screen + power_off on PWR long-press"
```
Then update `docs/firmware-notes.md`: add a "Power button" bullet (P6 power-hold at boot via `power_bsp`, PWR GPIO16 long-press -> power off; USB caveat that power-off only blanks the panel); commit `docs: power button / power-hold notes`.

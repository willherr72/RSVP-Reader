# Power Button + Power-Hold — Design Spec

**Date:** 2026-06-09
**Status:** Approved (brainstorm complete) — ready for implementation planning
**Target hardware:** Waveshare **ESP32-S3-Touch-LCD-3.49**
**Firmware stack:** ESP-IDF 5.5.2 + LVGL 9

---

## 1. Summary

The device currently can't power off, and on **battery** it powers on briefly then dies (it never latches its own power). This adds the missing power management:

1. **Power-hold** — assert the board's power-latch at boot so it stays on under battery.
2. **Power-off** — long-press the PWR button (~1.5 s) to power down.

**Definition of done:** on battery, the device stays on after the power button is released (boots to the reader and keeps running); a ~1.5 s long-press of PWR shows a brief "Powering off…" screen and powers the device down; on USB the reader is unaffected.

Out of scope (Phase 3): battery voltage/percentage readout, RTC, IMU, the BOOT button.

---

## 2. Hardware mechanism (from Waveshare `07_BATT_PWR_Test`)

- A **TCA9554 I/O expander** sits on **I2C0 (GPIO48 SCL / GPIO47 SDA, address 000)**.
- **Expander pin P6 is the power-hold:** `P6 = OUTPUT, HIGH` keeps the board powered (and keeps the LCD/board power rail up); `P6 = LOW` powers the board off (on battery).
- **PWR button = GPIO16**, active-low, plain GPIO input (BOOT = GPIO0, unused here).
- Waveshare asserts P6 high at startup and drives P6 low on a PWR long-press.

**Why this matters / prior bug:** the earlier SD bring-up created this same expander but drove the wrong pin (P1) and never asserted P6, so the driver's reset left P6 low and cut power a few seconds after boot (the "black screen"). Removing the expander entirely fixed USB (P6 floats to a default-on state) but means **battery never latches** — exactly the observed "won't stay on." The correct fix is to bring the expander back and **assert P6**.

---

## 3. Decisions (and rationale)

| Decision | Choice | Why |
|---|---|---|
| Power-off trigger | **Long-press PWR (~1.5 s)** | User choice; matches the hardware + Waveshare. Short/single press does nothing for now. |
| Scope | **Power-hold + power-off only** | Battery %/voltage (needs ADC + calibration) stays Phase 3. |
| Location | **New `power_bsp` component** | Matches `sdcard_bsp`/`i2c_bsp`; owns the TCA9554 + the I2C0 bus that RTC/IMU will share in Phase 3. |
| Button detection | **Debounced GPIO16 poll task** | One gesture (long-press); a counting poll is simpler than vendoring Waveshare's `multi_button` lib (YAGNI). |
| Power-off UX | **Brief "Powering off…" screen, then P6 low** | Confirms the action so the screen cutting doesn't look like a crash. |

---

## 4. Components

### 4.1 `firmware/components/power_bsp/` (new) — `power_bsp.h` / `power_bsp.c`
```c
typedef void (*power_shutdown_cb_t)(void);

// Create I2C0 (GPIO48/47) + TCA9554 (addr 000), assert P6 HIGH (power-hold),
// and start the PWR-button monitor task. Call FIRST in app_main.
void power_bsp_init(void);

// Drive expander P6 low -> board powers off (on battery).
void power_off(void);

// Register a callback fired when PWR is long-pressed (~1.5s). Runs in the button
// task context; it must hand off to the LVGL thread (it does not touch LVGL itself).
void power_bsp_set_shutdown_cb(power_shutdown_cb_t cb);
```
- **`power_bsp_init`**: `i2c_new_master_bus` on port 0 (GPIO48 SCL / GPIO47 SDA, internal pull-ups); `esp_io_expander_new_i2c_tca9554` (addr `ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000`); `set_dir(P6, OUTPUT)` + `set_level(P6, 1)`. Configure GPIO16 as input + pull-up. Spawn the button task. On any I2C/expander error: `ESP_LOGW` and return (don't brick).
- **Button task**: poll GPIO16 every ~20 ms. Count consecutive "pressed" (low) samples; at ≥ ~1.5 s (75 samples) of continuous press, fire the shutdown callback once, then wait for release before re-arming. A **boot guard** requires **one observed button release** before any long-press can fire — so the press that powered the device on (held through boot) cannot immediately power it back off.
- **`power_off`**: `esp_io_expander_set_level(P6, 0)`.
- Re-adds the managed dependency `espressif/esp_io_expander_tca9554: "^2"` (in `power_bsp/idf_component.yml`) and `REQUIRES esp_driver_i2c esp_io_expander esp_io_expander_tca9554` (removed from `sdcard_bsp` earlier).

### 4.2 `firmware/main/main.c`
- Call `power_bsp_init()` as the **very first** statement in `app_main` (before backlight/SPI/LCD setup), so P6 latches before the battery power circuit times out.

### 4.3 `firmware/main/ui_reader.cpp`
- Register a shutdown callback (via `power_bsp_set_shutdown_cb`) that hands off to the LVGL thread (atomic flag polled by a small `lv_timer`, since the button task must not touch LVGL): build a full-screen "Powering off…" label, then after one render (~a few hundred ms) call `power_off()`.

---

## 5. Behavior / data flow

```
boot → power_bsp_init(): I2C0 + TCA9554 + P6=HIGH (stay on) → backlight/LCD/SD/reader come up as today
button task: poll GPIO16 → >=1.5s continuous press → shutdown_cb
  → (LVGL thread) show "Powering off…" → power_off() (P6=LOW)
     on battery: board powers down.
     on USB: panel power cuts but the board stays alive (USB) — documented edge case.
```

## 6. Error handling
- TCA9554/I2C create failure → log + continue; on USB the board still runs (P6 floats on), only the battery-latch/power-off is unavailable. Never brick.
- Boot guard prevents the power-on long-press from immediately powering down.

## 7. Testing
- `power_bsp` is hardware glue (I2C/GPIO), like `sdcard_bsp` — no host test; the `core` engine stays the host-tested part.
- **On-device:** (a) on battery, the device stays on after releasing the power button and boots to the reader; (b) long-press PWR → "Powering off…" → powers down; (c) on USB, boot + reader behave exactly as today; (d) a short press does not power off.

## 8. Out of scope
- Battery voltage/percentage (Phase 3 "real battery": ADC + calibration).
- RTC (PCF85063) and IMU (QMI8658), which share this I2C0 bus + a future shared expander bring-up (Phase 3).
- The BOOT button (GPIO0) — reserved for the library/settings UI (Phase 2).
- A settings-menu "Power off" entry (Phase 2).

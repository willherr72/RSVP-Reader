# IMU Auto-Rotate — Design Spec

**Date:** 2026-06-10
**Status:** Approved (brainstorm complete) — ready for implementation planning
**Target hardware:** Waveshare **ESP32-S3-Touch-LCD-3.49** — **QMI8658** 6-axis IMU
**Firmware stack:** ESP-IDF 5.5.2 + LVGL 9, C++17 `core` (doctest)

---

## 1. Summary

Auto-flip the whole UI 180° when the device is turned end-over-end, using the QMI8658 accelerometer. Two landscape orientations only (LVGL rotation **90 ↔ 270**, both logical 640×172). An **Auto-rotate** toggle in Settings (default on) locks it.

**Definition of done:** flipping the held device 180° rotates the screen (reader/menu/settings) so text stays upright, after a brief debounce; it does **not** flip from small jitters or while lying flat; taps still land correctly in both orientations; the Settings toggle off locks the orientation; the choice persists.

**Out of scope:** portrait mode (would need a 172×640 layout), tilt/landscape-from-portrait, gyro use, screenshot/step features.

---

## 2. Hardware facts

- **QMI8658** at 7-bit I2C address **0x6b**, on **`power_bsp_i2c_bus()`** (I2C0, GPIO48/47 — the same bus as the TCA9554 and the PCF85063 RTC). The `i2c_bsp` `imu_dev_handle` scaffold (touch bus) is wrong/unused, exactly like the RTC was.
- Registers (auto-increment reads): `WHO_AM_I` 0x00 (=0x05), `CTRL1` 0x02 (set bit6 `ADDR_AI` for burst reads), `CTRL2` 0x03 (accel full-scale + ODR), `CTRL7` 0x08 (bit0 `aEN` enables the accelerometer). Accel output: `AccX_L` 0x35 … `AccZ_H` 0x3A (6 bytes, little-endian int16 per axis).
- Only the **sign** of one in-screen axis is needed for orientation, so no g-unit calibration — raw int16 is enough.

---

## 3. Components

- **`core` — `orient.{hpp,cpp}`** (new, host-tested, pure):
  ```cpp
  enum ScreenOrient { ORIENT_NORMAL, ORIENT_FLIPPED };
  // +axis beyond +deadzone -> NORMAL; below -deadzone -> FLIPPED; within deadzone -> keep `current`.
  ScreenOrient orientFromAxis(int axis, int deadzone, ScreenOrient current);
  ```
- **`firmware/components/imu_bsp/`** (new, **C++** — `imu_bsp.hpp`/`.cpp`), depends on `power_bsp`:
  ```cpp
  void imu_init();                              // add 0x6b + enable accel (CTRL1/2/7)
  bool imu_read_accel(int16_t& ax, int16_t& ay, int16_t& az);   // false on I2C error
  ```
- **`firmware/main/orient.{h,cpp}`** (new): `orient_start()` creates a ~5 Hz `lv_timer` (LVGL thread). Each tick: `imu_read_accel`; pick the calibrated in-screen axis; `orientFromAxis(axis, DEADZONE, current)`; require the same result for **~0.6 s** (3 consecutive ticks) before committing; on a committed change *and* `settings().auto_rotate`, call `lv_display_set_rotation(lv_display_get_default(), target)` (90 or 270). Started from `ui_menu_init` (after `imu_init`).
- **`firmware/main/app_settings`**: add `bool auto_rotate = true;` (NVS key `"rot"`).
- **`firmware/main/ui_menu.cpp`**: an **"Auto-rotate"** switch in Settings (and `imu_init()` in `ui_menu_init`).

---

## 4. Orientation logic

The IMU mounting determines which accel axis flips sign between the two landscapes; this is **picked empirically during bring-up** (read accel in both orientations, choose the axis + sign so +axis = NORMAL = rotation 90). Constants: `DEADZONE` (~1/4 g in raw counts) so a near-flat device doesn't flip; the **3-tick hold** (~0.6 s) absorbs jitter/motion. When `auto_rotate` is off, the timer reads but never calls `lv_display_set_rotation` (orientation locked at its current value).

## 5. Touch across the flip

Both rotations are logical 640×172, so no relayout. The flush rotates by `lv_display_get_rotation()`, and LVGL rotates indev input by the display rotation, so feeding the same calibrated coords should make taps follow the flip (LVGL's 90 and 270 input transforms are 180° opposites). **Verify on-device after a flip**; if a tap is mirrored, add a 180° flip of the logical point in `TouchInputReadCallback` when rotation == 270.

## 6. Error handling

- I2C read failure → the tick is skipped (orientation unchanged); no crash.
- `imu_init` failure → `imu_read_accel` returns false; the screen just never auto-rotates.

## 7. Testing

- **Host (TDD):** `orientFromAxis` — positive/negative beyond deadzone, both signs; within-deadzone keeps `current` (both NORMAL and FLIPPED inputs).
- **On-device:** flipping the device rotates the whole UI after the hold; no flip when lying flat or from small jitter; **Auto-rotate off locks** it (persists across reboot); taps land correctly in both orientations; the clock/power button/reading all keep working rotated.

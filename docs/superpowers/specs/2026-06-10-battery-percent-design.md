# Real Battery % — Design Spec

**Date:** 2026-06-10
**Status:** Approved (brainstorm complete) — ready for implementation planning
**Target hardware:** Waveshare **ESP32-S3-Touch-LCD-3.49** — battery sensed on **GPIO4 / ADC1 ch3**
**Firmware stack:** ESP-IDF 5.5.2 (`esp_adc`), LVGL 9, C++17 `core` (doctest)

---

## 1. Summary

Show the real battery percentage in the reader's top bar (replacing nothing — it's added left of the clock), read from the battery-sense ADC.

**Definition of done:** the reader's top-right shows a battery percentage (e.g. `84%`) next to the clock, derived from the actual battery voltage and refreshed periodically; it reads a sane value and falls as the battery discharges.

**Out of scope:** a battery icon (user chose plain %), a charging indicator / USB-vs-battery UI, low-battery warnings, battery in the menu/settings screens, fuel-gauge coulomb counting.

---

## 2. Hardware facts (confirmed against Waveshare schematic + demo `adc_bsp.c`)

- Battery is on net **`BAT_ADC` = GPIO4 = ADC1 channel 3** (`ADC_UNIT_1`, `ADC_CHANNEL_3`).
- **÷3 divider** (R33 200K / R113 100K), so `battery_mV = adc_pin_mV × 3`.
- Read with **`ADC_ATTEN_DB_12`**, 12-bit, and **curve-fitting calibration** (`adc_cali_create_scheme_curve_fitting`) → `adc_cali_raw_to_voltage` gives calibrated mV at the pin.
- A LiPo cell is ~3.3 V (empty) to 4.2 V (full); on USB the pin reads the charge voltage (~4.1–4.2 V → ~100%), which is acceptable.

---

## 3. Components

- **`core` — `battery.{hpp,cpp}`** (new, host-tested, pure):
  ```cpp
  int batteryPercent(int mv);   // battery millivolts -> 0..100 via a piecewise LiPo curve
  ```
  Piecewise-linear over points like {3300→0, 3500→8, 3700→25, 3800→40, 3900→58, 4000→76, 4100→90, 4200→100}, clamped to [0,100]. (A plain linear 3300–4200 map overstates mid-charge badly; the curve matters.)
- **`firmware/components/battery_bsp/`** (new, **C** — `battery_bsp.h`/`.c`), `REQUIRES esp_adc`:
  ```c
  void batt_init(void);     // ADC1 oneshot ch3 + 12dB + 12-bit + curve-fitting cali
  int  batt_read_mv(void);  // average ~16 reads -> calibrated mV x3 (battery mV); 0 on failure
  ```
- **`firmware/main/ui_reader.cpp`**: a `g_batt` label placed left of the clock (top-right). The existing clock `lv_timer` (~20 s) also updates it: `int mv = batt_read_mv(); if (mv > 0) "<batteryPercent(mv)>%" else hide/blank`.
- **`firmware/main/ui_menu.cpp`** (`ui_menu_init`): call `batt_init()` at boot (alongside `rtc_init`/`imu_init`).

---

## 4. Data flow

Boot: `batt_init()` (ADC + calibration). Reader: the clock/status timer (~20 s) calls `batt_read_mv()` → `batteryPercent()` → updates the `g_batt` label. `g_batt` lives in the same top-right cluster as the clock so they sit side by side and re-flow as text changes.

## 5. Error handling

- `batt_init` failure (ADC/cali create) → `batt_read_mv` returns 0 → the label is left blank (no `%` shown), no crash.
- A single bad read returns 0 → label blanked that tick; the next tick recovers.

## 6. Testing

- **Host (TDD):** `batteryPercent` — endpoints (≤3300→0, ≥4200→100), a few interior points hit the curve (e.g. 3700→~25, 3900→~58), monotonic non-decreasing across the range.
- **On-device:** the reader shows a plausible % matching the charge level; sits ~100% on USB; the value updates and trends down on battery as it discharges; ADC read doesn't disturb anything else (GPIO4 is otherwise unused).

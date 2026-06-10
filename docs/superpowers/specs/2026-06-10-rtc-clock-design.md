# RTC + Clock — Design Spec

**Date:** 2026-06-10
**Status:** Approved (brainstorm complete) — ready for implementation planning
**Target hardware:** Waveshare **ESP32-S3-Touch-LCD-3.49** — **PCF85063** RTC on the touch I2C bus
**Firmware stack:** ESP-IDF 5.5.2 + LVGL 9, C++17 `core` (doctest)

---

## 1. Summary

Bring up the PCF85063 real-time clock so the reader shows the actual time (replacing the hardcoded `"2:14"`), and add a **Set clock** screen in Settings. 12-hour display ("2:14 PM"); the set screen sets **time only** (HH:MM).

**Definition of done:** the reader's top-right shows the real time, updating each minute; Settings → "Set clock" sets the time and it sticks (RTC keeps time across reboots); an un-set / power-lost RTC shows `--:--`.

**Out of scope:** setting the date (the chip tracks it; not exposed), a 12h/24h toggle, showing the clock outside the reader, alarms/timers. (Estimated-time-to-finish and WiFi Drop are separate features.)

---

## 2. Hardware facts

- **PCF85063** at 7-bit I2C address **0x51**, on **`user_i2c_port1_handle`** (I2C_NUM_1, GPIO18 SCL / GPIO17 SDA) — the touch bus. `i2c_bsp` already declares `rtc_dev_handle` (currently NULL/unused). The TCA9554/power bus (I2C0, GPIO48/47) is unrelated.
- Registers: `Control_1` 0x00 (bit1 `12_24`: 0 = 24-hour mode — we use this), `Seconds` 0x04 (**bit7 = OS** oscillator-stop flag; bits6-0 = BCD seconds), `Minutes` 0x05 (BCD), `Hours` 0x06 (BCD, 24h), `Days` 0x07 … `Years` 0x0A.
- **OS flag:** set by the chip when the oscillator has stopped (power loss without VBAT backup) → the time is invalid. `set` writes seconds with bit7 = 0 to clear it.

---

## 3. Components

- **`core` — `rtctime.{hpp,cpp}`** (new, host-tested, pure):
  ```cpp
  struct RtcTime { std::uint8_t hour, minute, second; };   // hour 0..23
  std::uint8_t bcdToBin(std::uint8_t bcd);
  std::uint8_t binToBcd(std::uint8_t bin);
  std::string  formatClock12h(std::uint8_t hour24, std::uint8_t minute);  // "2:14 PM", "12:05 AM"
  std::uint8_t to24h(std::uint8_t hour12 /*1..12*/, bool isPM);           // for the set screen
  ```
- **`firmware/components/rtc_bsp/`** (new, **C++** — `rtc_bsp.hpp`/`.cpp`): the I2C glue, depends on `i2c_bsp` (C header for `user_i2c_port1_handle`/`rtc_dev_handle`) + `core` (`rtctime.hpp` for `RtcTime`).
  ```cpp
  void rtc_init();                     // add the 0x51 device to user_i2c_port1_handle; set 24h mode
  bool rtc_valid();                    // false if the OS flag is set (time never set / power-lost)
  bool rtc_get(rsvp::RtcTime& out);    // read sec/min/hour (BCD->bin); false on I2C error
  bool rtc_set(const rsvp::RtcTime& t);// write sec(OS cleared)/min/hour (bin->BCD); false on error
  ```
  C++ so it can use the core `RtcTime` directly; callers (`ui_reader`, `ui_menu`) are C++.
- **`firmware/main/ui_reader.cpp`**: the clock label becomes a tracked object (`g_clk`). A persistent `lv_timer` (~20 s) reads `rtc_get` and sets the label to `formatClock12h(...)`, or `"--:--"` when `!rtc_valid()` / read fails. (The label is rebuilt per reader screen; the timer no-ops when no reader is up.)
- **`firmware/main/ui_menu.cpp`**: a **"Set clock"** action row in Settings (same pattern as "Calibrate touch") → `SCR_SETCLOCK` screen with **hour (1–12), minute (0–59), AM·PM** steppers (pre-filled from `rtc_get`, or 12:00 AM if invalid) + a **"Set"** button → `rtc_set(to24h(...))` → back to the menu. BOOT cancels.
- **`firmware/main/ui_menu.cpp`** (`ui_menu_init`): call `rtc_init()` at boot — `ui_menu_init` runs under the LVGL lock *after* `app_main` has already done `touch_i2c_master_Init()` (the bus exists), and it's C++ so it can call the C++ `rtc_bsp`. (`main.c` is unchanged.)

---

## 4. Data flow

Boot: `touch_i2c_master_Init` (creates the bus) → `rtc_init` (adds the device, reads OS). Reader: the clock timer polls `rtc_get` every ~20 s → label. Set: steppers edit a local `RtcTime` → "Set" → `rtc_set` writes the chip (seconds reset to 0, OS cleared) → the reader's next poll shows it.

## 5. Error handling

- I2C read fails or `!rtc_valid()` → clock shows `"--:--"`.
- I2C write (`rtc_set`) fails → logged; the set screen still returns to the menu (no crash).
- `rtc_init` failure (device add) → `rtc_get/set` return false; clock stays `"--:--"`.

## 6. Testing

- **Host (TDD):** `bcdToBin`/`binToBcd` (incl. round-trip), `formatClock12h` (midnight→"12:00 AM", noon→"12:00 PM", 13:05→"1:05 PM", zero-padded minutes), `to24h` (12 AM→0, 12 PM→12, 1 PM→13).
- **On-device:** clock shows the real time + advances; Set clock sets it; value survives a reboot; invalid/power-lost RTC shows `--:--`; touch-bus (touch) still works alongside the RTC device.

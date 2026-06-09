# Firmware build & flash notes — ESP32-S3-Touch-LCD-3.49

## Toolchain (Windows)
- ESP-IDF **v5.5.2** at `C:\Users\WilliamHerr\esp\v5.5.2\esp-idf`.
- **Gotcha:** the system Python (3.14) shadows ESP-IDF's bundled venv, so
  `export.ps1` looks for the wrong `idf5.5_py3.14_env` and fails. Set
  `IDF_PYTHON_ENV_PATH` to the real py3.11 venv first. Env does not persist
  between tool shells, so every command re-activates:

  ```powershell
  $idf = "C:\Users\WilliamHerr\esp\v5.5.2\esp-idf"
  $env:IDF_PATH = $idf
  $env:IDF_PYTHON_ENV_PATH = "C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env"
  & "$idf\export.ps1" *> $null
  Set-Location <repo>\firmware
  idf.py build
  idf.py -p COM32 flash      # check the actual COM port; board uses native USB-Serial-JTAG (VID 303A/PID 1001)
  ```
  (`idf.py --version` prints `v1.0.3` from the idf-exe shim — harmless; the real
  IDF version is 5.5.2.)

## Project layout
- `core/` — host-tested C++ engine, also registered as an ESP-IDF component via
  `core/CMakeLists.txt` (builds orp/pacing/player/tokenize on-device; the host
  test build references the sources directly and ignores that file).
- `firmware/` — forked from the Waveshare ESP32-S3-Touch-LCD-3.49 ESP-IDF
  `10_LVGL_V9_Test` demo. `main/main.c` = board bring-up (AXS15231B QSPI panel,
  touch, octal PSRAM, backlight PWM), rotated to landscape 640x172.
  `main/ui_reader.cpp` = the RSVP reading screen driving the `Player`.
- Host tests: `cmake --build build/host && build/host/Debug/rsvp_tests.exe`
  (VS 2022 generator; 66 cases at last check).

## Status
- ✅ Live reader works on hardware: word stream at the set WPM, red ORP letter
  pinned to the focal column, prev/next flankers, battery/clock + wpm/%.
- ✅ Touch **input is reliable; gesture controls live**: tap = play/pause,
  swipe up/down = WPM ±25 (clamped 100–800), swipe left/right = sentence
  (swipe-left = previous, swipe-right = next). See spec/plan dated 2026-06-09.
  - **Root cause of the old intermittent touch** (confirmed on-device, not the
    hand-rolled read — that worked fine in the demo): the touch indev was polled
    on LVGL's single render thread, and continuous full-frame redraws (a 30fps
    debug label + the `LV_USE_PERF_MONITOR` overlay + FULL-mode QSPI flush)
    starved the poll to 8–15 Hz, where LVGL's pointer state machine corrupted
    presses into merged/garbage gestures.
  - **Fix:** (1) cut render load — `LV_USE_PERF_MONITOR`/`SYSMON` off, redraw only
    on word change; (2) drive touch via `esp_lcd_touch_new_i2c_axs15231b`
    (declared in `esp_lcd_axs15231b.h`, **not** a separate touch header; the
    `…CONFIG_EX` macro is buggy — set `scl_speed_hz` on the struct) on the shared
    I2C bus; (3) sample in a dedicated ~100 Hz task into a spinlock snapshot the
    LVGL read_cb copies, and lower `CONFIG_LV_DEF_REFR_PERIOD` 33→10 ms → ~64 Hz
    indev, 0 merges; (4) bridge brief mid-drag touch dropouts (~80 ms hold) so a
    swipe doesn't fragment into taps; (5) classify via host-tested
    `core/rsvp/gesture.hpp`.
  - **Coordinates:** feed LVGL **logical** (pre-rotation 172×640) coords via the
    driver's `swap_xy`; LVGL's 90° rotation maps them to the panel. The touch
    **X-axis is currently screen-relative (mirrored vs. physical)** — invisible
    for the gesture-only reader, but **set `mirror_y=1`** in the `esp_lcd_touch_config_t`
    when adding position-dependent UI (library/settings buttons) so taps line up.

## Next features (see docs/superpowers/specs/ + plans/)
SD card mount + real EPUB/TXT loading (register the rest of `core` — index/
entity/htmltext/opf/epub/zipreader — plus vendored miniz as firmware
components), library + settings screens, IMU auto-rotate, real battery, Wi-Fi
upload.

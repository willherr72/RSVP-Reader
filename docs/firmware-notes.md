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
- ✅ Touch **coordinate** transform fixed: the AXS15231B raw coordinates are
  already landscape-oriented (X 0–640, Y 0–172), so the demo's ROT_90
  swap/clamp was wrong — pass them through directly (see `TouchInputReadCallback`).
- ⚠️ Touch **input is intermittent**: the demo's hand-rolled I2C read
  (`read_touchpad_cmd = {0xb5,0xab,0xa5,0x5a,...}`) misses quick taps and only
  sometimes registers a swipe, so tap/swipe controls are unreliable.
  `ui_reader.cpp` currently carries on-screen `P=/R=/dx=/dy=` debug counters used
  to diagnose this (a single tap produced **no** press/release events).
  **Next step:** replace the hand-rolled read with the proper `esp_lcd_touch`
  AXS15231B driver (already pulled in as a managed_component) for reliable input,
  then re-enable tap=play/pause, swipe-up/down=WPM, swipe-left/right=sentence.

## Next features (see docs/superpowers/specs/ + plans/)
SD card mount + real EPUB/TXT loading (register the rest of `core` — index/
entity/htmltext/opf/epub/zipreader — plus vendored miniz as firmware
components), library + settings screens, IMU auto-rotate, real battery, Wi-Fi
upload.

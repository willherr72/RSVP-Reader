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
- **exFAT build prerequisite (IDF edit):** SD cards >32 GB are exFAT, which ESP-IDF's
  FatFs disables by default. Set `FF_FS_EXFAT 1` in
  `…/esp-idf/components/fatfs/src/ffconf.h` (it's a hardcoded `0`, no Kconfig) and
  enable `CONFIG_FATFS_USE_LABEL=y` (in `sdkconfig.defaults`) so the exFAT label path
  compiles. **This ffconf edit is not in the repo — re-apply it after any IDF
  reinstall.** (exFAT carries Microsoft patent terms; that's why Espressif ships it off.)

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
- ✅ **SD card + book loading** (spec/plan dated 2026-06-09): mounts the SDMMC TF
  card (1-line, **CLK41/CMD39/D040**, `/sdcard`) at boot via `sdcard_bsp`; the
  full `core` document pipeline (index/entity/htmltext/opf/epub/zipreader) is
  registered for firmware via a `miniz` component (note: include miniz as
  `"miniz/miniz.h"` — a bare `"miniz.h"` collides with ESP-IDF's inflate-only
  `esp_rom/include/miniz.h`). `book_loader` scans `/sdcard` for the first
  `.epub`/`.txt`, compile-and-caches a compiled index to `/sdcard/.rsvp/<name>.idx`
  (gated by `indexMatchesSource`), and the reader reads it (title shown top-left),
  falling back to the built-in sample on any failure.
  - **SD only needs exFAT** (no I/O-expander setup): the SD bus is powered by default,
    so `sdcard_bsp` is a plain 1-line SDMMC mount. Cards >32 GB are **exFAT** — see the
    exFAT build prerequisite above; that was the real cause of the early mount failures.
  - **Verified on-device:** Project Hail Mary (1.27 MB EPUB, **149,440 words**)
    stream-compiles in a background task behind a "Loading…" screen, caches to
    `/sdcard/.rsvp/`, and reads on demand; reboot hits the cache instantly (exFAT
    64 GB card). Host tests green; no-card fallback → sample.
  - **Bounded memory (streaming index + on-demand reader):** `epubToIndex` feeds an
    incremental `IndexBuilder` (never the whole ~5 MB `Document`), and the
    `Player`/reader pull words on demand via `CompiledIndex.at(i)` — peak RAM ~1–2 MB
    regardless of book size. The compile runs in a **dedicated 48 KB-stack task**
    (`load_task` in `ui_reader.cpp`); main task is back to 16 KB.
  - ⚠️ **Three device-only gotchas that cost a long debug** (all invisible on host):
    - **miniz ↔ ESP32-S3 ROM symbol collision.** The mask ROM exports `tinfl_*`/`tdefl_*`
      from an older miniz with a different `tinfl_decompressor` layout; the linker bound
      our miniz's `tinfl_decompress` call to the ROM copy, so our 32 KB struct was read
      with the wrong layout → memory corruption (a 244-byte inflate crashed,
      `StoreProhibited`/BREAK). **Fixed by renaming our copies via `-D` in
      `firmware/components/miniz/CMakeLists.txt`.** (Originally misdiagnosed as OOM.)
    - **Inflate needs a big stack:** miniz's `tinfl_decompressor` is ~32 KB on the stack,
      so a 64 KB task stack won't fit the largest free internal block once LVGL is up
      (~52 KB); 48 KB does.
    - **TCA9554 P6 is the power-hold.** A speculative SD-power init drove the *wrong* pin
      (P1) and never asserted **P6**, so the driver's reset left P6 low and cut **LCD power**
      ~2-3 s after boot (Loading screen → fully dark). The fix is to assert **P6 high**, which
      `power_bsp` now does (the power button, below). SD itself needs no expander at all.
  - Resume-on-reopen + library/browse UI remain Phase 2.
- ✅ **Power button / power-hold** (spec/plan dated 2026-06-09): `power_bsp` creates the
  I2C0 (GPIO48/47) TCA9554 (addr 000) and asserts **P6 high** as the first thing in
  `app_main`, so the device stays powered on **battery** (it died before) and the panel
  stays up. The **PWR button (GPIO16)** is polled for a **~1.5 s long-press** (release-first
  boot guard) → "Powering off…" screen → `power_off()` drives **P6 low** to shut down.
  - **USB caveat:** on USB the board stays powered regardless of P6, so a long-press just
    shows "Powering off…" and freezes there; real power-down only happens on battery.
  - Battery %/voltage (ADC), IMU, and a menu "Power off" entry are still Phase 3.
- ✅ **Menu / Library / Settings (Phase 2A)**: BOOT button (GPIO0, short-press in
  `power_bsp`) opens a tile menu over the reader; Library lists SD books (title/author/%,
  per-book resume via `.pos`); Settings (font/WPM/brightness/flankers/resume/start-paused)
  persist in NVS. On-device gotchas learned here:
  - **Touch calibration:** the AXS15231B's reported coords need an **affine** map to the
    rotated 640×172 logical space, and **LVGL rotates indev input** (native 172×640 →
    logical) as `hit = (640 - fed_y, fed_x)` — feed the inverse. Calibration is a
    re-runnable Settings action (3 crosshairs → Cramer affine → NVS, `touch_cal.h`).
    `lv_indev_set_scroll_limit(80)` so the jittery touch (~44px drift per tap) isn't read
    as a scroll.
  - **Backlight is inverted:** `LCD_PWM_MODE_255 = 0xff-255 = 0`, so `setUpduty(0)` =
    brightest, `255` = off.
  - The 48KB book-load task must be created **once at boot** (`rsvp_reader_init`); spawning
    it per-open fails once the UI has fragmented internal RAM.
- ✅ **RTC + clock (Phase 3, dated 2026-06-10)**: **PCF85063 at 0x51 on the I2C0 power bus**
  (GPIO48/47, with the TCA9554) — **not** the touch bus the `i2c_bsp` `rtc_dev_handle`
  scaffold implied (touch bus only had 0x3b). `power_bsp_i2c_bus()` exposes the I2C0 handle;
  `rtc_bsp` adds the device there. 12-hour clock in the reader; "Set clock" in Settings;
  pure conversions in host-tested `core/rtctime`.

## Next features (see docs/superpowers/specs/ + plans/)
**WiFi Drop** (AP + web upload of EPUB/TXT to the SD), then IMU auto-rotate, real battery
(%/ADC), estimated-time-to-finish.

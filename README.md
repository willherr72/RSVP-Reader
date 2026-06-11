# RSVP-Reader

A pocket **speed reader** for the Waveshare **ESP32-S3-Touch-LCD-3.49**. Books play one word
at a time (RSVP — Rapid Serial Visual Presentation) with the focal letter pinned under a
tick mark, so your eyes never move. Load EPUBs over WiFi, read at 100–800 WPM, flip the
device and the screen follows.

![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue) ![Framework](https://img.shields.io/badge/ESP--IDF-5.5.2-green) ![UI](https://img.shields.io/badge/LVGL-9-purple)

## Features

- **RSVP reading** — ORP-aligned focal letter with tick marks, optional leading/trailing
  flanker words, punctuation-aware pacing, three font sizes.
- **EPUB + TXT from microSD** — books compile once into a compact index cached on the card
  (`/.rsvp/`), then open instantly; per-book resume; smart-quote/dash normalization.
- **WiFi Drop** — the device hosts a WPA2 access point + a web page: drop `.epub`/`.txt`
  files onto the card from your phone or laptop (upload progress, delete), no card-pulling.
- **Touch UI** — library and settings screens; on-device 3-point touch calibration
  (persisted) for any panel.
- **Gestures while reading** — tap: pause/play · swipe up/down: speed ±25 WPM ·
  swipe left/right: next/previous sentence.
- **Auto-rotate** — QMI8658 accelerometer flips the screen 180° when you turn the device
  (touch follows automatically); lockable in Settings.
- **Real clock** — PCF85063 RTC, 12-hour display, set from Settings, keeps time across reboots.
- **Battery %** — real voltage via ADC + a LiPo discharge curve.
- **ETA** — time left to finish the book from words remaining ÷ current WPM (`2h 15m`).
- **Power button** — long-press PWR (~1.5 s) to power off on battery; USB-safe recovery.

## Hardware

| Part | Detail |
|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-3.49 (16 MB flash, 8 MB octal PSRAM) |
| Display | 3.49" 640×172 bar LCD, AXS15231B over QSPI (40 MHz), used landscape |
| Touch | AXS15231B capacitive, I²C1 (GPIO18/17) |
| RTC / IMU / power-hold | PCF85063 (0x51) / QMI8658 (0x6b) / TCA9554 (0x20) — all on I²C0 (GPIO48/47) |
| Battery sense | GPIO4 = ADC1 ch3, ÷3 divider |
| Storage | microSD (SDMMC 1-line) at `/sdcard` |

## Architecture

```
core/        C++17 reading engine — no ESP dependencies, fully host-tested
             tokenizer · ORP · pacing · player · gestures · EPUB→compiled-index
             pipeline (zip/OPF/XHTML via vendored miniz) · clock/battery/ETA helpers
firmware/    ESP-IDF 5.5.2 + LVGL 9 app and board-support components
             (power, SD, RTC, IMU, battery, WiFi Drop) — `core` builds as a component
test/host/   doctest suite for core (539 assertions) + the rsvp_compile CLI tool
docs/        firmware-notes.md (hard-won hardware lessons) · superpowers/ specs & plans
third_party/ miniz
```

The flush path rotates the LVGL frame into the panel's native portrait orientation with a
fused rotate+swap+chunk pass (see `docs/firmware-notes.md` for why — and for the other
hardware gotchas: inverted backlight PWM, touch affine calibration, WiFi-vs-SD DMA RAM).

## Building

### Host tests (no hardware)

```powershell
cmake -B build/host -S test/host
cmake --build build/host --config Debug
build/host/Debug/rsvp_tests.exe
```

`rsvp_compile.exe <book.epub>` (same build) runs an EPUB through the real pipeline on the
PC — handy for separating "bad file" from "device problem".

### Firmware

ESP-IDF 5.5.2 with the board on a COM port:

```powershell
idf.py -C firmware build
idf.py -C firmware -p COM32 flash
```

## Using it

- Boot opens the **menu**: Library · Settings · WiFi Drop. The side **Menu button** (BOOT)
  reopens it anytime; reading pauses and your position is saved.
- **WiFi Drop**: open it, join the `RSVP-Reader` network (password on screen), browse to
  `http://192.168.4.1`, drop books on. (Android: accept "stay connected without internet".)
- **Settings**: speed, font size, brightness, flanker words, resume/start-paused,
  auto-rotate, ETA, touch calibration, clock.
- Books up to ~5 MB compile on-device; bigger image-heavy EPUBs are rejected gracefully —
  strip the images on a PC first (the text is what matters to an RSVP reader).

# ETA to Finish — Design Spec

**Date:** 2026-06-10
**Status:** Approved (brainstorm complete) — ready for implementation planning
**Target hardware:** Waveshare ESP32-S3-Touch-LCD-3.49
**Firmware stack:** ESP-IDF 5.5.2 + LVGL 9, C++17 `core` (doctest)

---

## 1. Summary

Show the estimated time left to finish the book in the reader's bottom-right, computed from words remaining and the current reading speed. A Settings toggle hides it.

**Definition of done:** the reader's bottom-right shows a time-remaining readout (e.g. `2h 15m`) that counts down as you read and updates instantly when you change WPM; a "Show ETA" setting (default on) hides it; no RTC involved — it's pure arithmetic.

**Out of scope:** a clock-time "finish by 3:40 PM" readout (the RTC exists but the user wants duration only), accounting for pauses/reading breaks, per-section ETAs.

---

## 2. Formula + format

- `remainingWords = g_index.wordCount() − g_player->index()`; `wpm = g_wpm` (the live speed).
- `etaString(remainingWords, wpm)`:
  - `wpm <= 0` or `remainingWords <= 0` → `""` (nothing left / not started → blank, no "Done").
  - `mins = round(remainingWords / wpm)` (nearest whole minute).
  - `mins < 1` → `"<1m"`; `mins < 60` → `"<mins>m"` (e.g. `45m`); else `h = mins/60, m = mins%60` → `"<h>h"` when `m == 0` (e.g. `2h`), otherwise `"<h>h <m>m"` (e.g. `2h 15m`).

---

## 3. Components

- **`core` — `eta.{hpp,cpp}`** (new, host-tested, pure):
  ```cpp
  std::string etaString(int remainingWords, int wpm);
  ```
- **`firmware/main/ui_reader.cpp`**: a `g_eta` label at `LV_ALIGN_BOTTOM_RIGHT` (the existing `g_wpm_lbl` is `BOTTOM_MID`). The existing bottom-status refresh (`refresh_status`, called as the word index advances and on pause/WPM change) also sets `g_eta`: if `settings().show_eta` → `etaString(wordCount()-index(), g_wpm)`, else `""`. Also refreshed by `rsvp_reader_apply_settings()` so the toggle applies live.
- **`firmware/main/app_settings`**: add `bool show_eta = true;` (NVS key `"eta"`).
- **`firmware/main/ui_menu.cpp`**: a **"Show ETA"** switch in Settings (drives `settings().show_eta`, saves, and triggers a reader refresh).

---

## 4. Data flow

As each word advances, the reader's tick calls `refresh_status`, which now also computes `etaString` from the remaining word count and current WPM and writes the `g_eta` label. Changing speed (swipe up/down → `set_wpm`) refreshes it immediately. Toggling "Show ETA" in Settings saves the flag and refreshes so it shows/hides without reopening the book.

## 5. Error handling

- No player / zero word count → `refresh_status` already guards; `g_eta` stays blank.
- `wpm <= 0` (shouldn't happen — WPM is floored elsewhere) → `etaString` returns `""`.

## 6. Testing

- **Host (TDD):** `etaString` at wpm=300 — `40500→"2h 15m"`, `36000→"2h"` (exact hour), `13500→"45m"`, `100→"<1m"`, and the empty cases (`remainingWords<=0` → `""`, `wpm<=0` → `""`).
- **On-device:** ETA shows bottom-right and decreases while reading; swipe up/down (WPM change) immediately changes it; near the end shows `<1m` then blanks at 100%; **Show ETA off** hides it live and persists across reboot.

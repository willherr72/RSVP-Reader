# Menu + Library + Settings (Phase 2A) — Design Spec

**Date:** 2026-06-09
**Status:** Approved (brainstorm complete) — ready for implementation planning
**Target hardware:** Waveshare **ESP32-S3-Touch-LCD-3.49** (640×172 landscape, rotated)
**Firmware stack:** ESP-IDF 5.5.2 + LVGL 9

---

## 1. Summary

Today the device auto-opens the first SD book and is gesture-only. This adds the **menu system**: a BOOT-button menu with **Library** (browse + open any book, resume per book), **Settings** (reader options, persisted), and a stubbed **WiFi Drop** entry.

**Definition of done:** on boot the device shows the Menu; the Library lists SD books (title + author + % read) and opens any of them (resuming position); Settings changes (speed, font size, brightness, leading/trailing words, resume toggle) apply live and survive a reboot; the BOOT button navigates everywhere; taps land correctly on the new UI.

**Out of scope** (follow-ups, in order): RTC + real clock + "Set clock" screen; estimated-time-to-finish; WiFi Drop implementation.

---

## 2. Decisions (UI approved via mockups)

| Decision | Choice |
|---|---|
| Menu layout | **Horizontal tiles**: Library · Settings · WiFi Drop (three big tap targets) |
| Library row | Title + author (left), **% complete** (right), thin red progress bar (bottom) |
| Settings controls | **Steppers** (− value +) for numeric, **switches** for on/off; one scrollable list |
| Boot screen | **Menu** (books load on demand when selected) |
| Navigation | **BOOT = back/menu** (no on-screen back target) |
| Power-off | unchanged (PWR long-press, from any screen) |
| Settings scope (2A) | speed (WPM), font size, brightness, leading/trailing words, resume-on-open |

---

## 3. Navigation model

States: `MENU`, `LIBRARY`, `SETTINGS`, `WIFI` (stub), `READER`. The BOOT button (GPIO0, short press) drives transitions; touch taps drill in.

```
boot ───────────────► MENU
MENU  tap Library  ─► LIBRARY      LIBRARY  tap book ─► (load) ─► READER
MENU  tap Settings ─► SETTINGS     LIBRARY/SETTINGS/WIFI  BOOT ─► MENU
MENU  tap WiFi     ─► WIFI(stub)
MENU  BOOT ─► READER (if a book is open; else stay on MENU)
READER BOOT ─► MENU
```

So BOOT **toggles Reader↔Menu** while reading, and is **back-to-Menu** from a sub-screen. The reader's existing touch gestures (tap = play/pause, swipes = WPM/sentence) are unchanged — BOOT is a separate hardware button, no conflict. Only one screen is "active" at a time (others are deleted/hidden to save RAM).

---

## 4. Components

Splits the growing `ui_reader.cpp` by responsibility.

- **`firmware/main/ui_reader.cpp`** (trimmed): the **reader screen** only — word/ORP render, gestures, tick, and the existing loading screen (now used when *opening* a book, not at boot). Exposes `rsvp_open_book_path(const std::string& path)` (show the loading screen → background `load_book(path)` → build the reader, resuming from `.pos` when enabled) and `rsvp_reader_save_position()` (write the open book's current word index to its `.pos`).
- **`firmware/main/ui_menu.{h,cpp}`** (new): the **Menu / Library / Settings / WiFi-stub** screens + the navigation state machine. Owns the screen stack and reacts to the BOOT-button callback. Builds the Library from `book_loader`, opens a selected book (loading screen → `load_book` → `rsvp_open_book`), and renders/edits Settings.
- **`firmware/main/app_settings.{h,cpp}`** (new): a `Settings` struct (wpm, font size enum, brightness, show_flankers, resume_on_open) with `settings_load()` / `settings_save()` over **NVS** (namespace `rsvp`). One in-RAM instance; getters/setters used by the reader + settings screen.
- **`firmware/main/book_loader.{hpp,cpp}`** (extended): see §5/§6 — `list_books()`, `load_book(path)`, `.idx`-header metadata read, `.pos` save/restore.
- **`firmware/components/power_bsp`** (extended): the existing button task also polls **GPIO0 (BOOT)** for a short press and fires `power_bsp_set_boot_cb(cb)` (the menu/back action). The callback only flips an atomic flag; an `lv_timer` in `ui_menu` performs the LVGL transition.
- **`firmware/main/main.c`**: `app_main` shows the **Menu** at the end (instead of the loading screen): `settings_load()` then `ui_menu_create()`.
- **`core`**: add a host-tested `CompiledIndex::readHeader(bytes) → {title, author, wordCount, ok}` that parses only the fixed header (no token stream) for fast Library listing.

---

## 5. Library

**Data (fast listing — no full compile):** scan `/sdcard` for `.epub`/`.txt`. For each:
- If `/sdcard/.rsvp/<name>.idx` exists, read **only its header** (`CompiledIndex::readHeader`) → title, author, wordCount.
- Else show the **filename** as the title (compiled on first open).
- Read `/sdcard/.rsvp/<name>.pos` (a little-endian `uint32` word index) if present → `percent = pos * 100 / wordCount` (0 if unknown).
- A built-in **"Sample"** entry is always appended so there's always something to open (first run / empty card).

`book_loader` API:
```cpp
struct BookEntry { std::string path, title, author; uint32_t wordCount, position; };
std::vector<BookEntry> list_books();          // SD scan + .idx header + .pos
std::optional<LoadedBook> load_book(const std::string& path);  // compile/cache+parse one book
void     save_position(const std::string& bookPath, uint32_t wordIndex);  // -> <name>.pos
uint32_t load_position(const std::string& bookPath);                      // 0 if none/invalid
```
(`load_first_book()` is reimplemented as `load_book(find_first_book())` or removed.) `ui_reader` calls `save_position` from `rsvp_reader_save_position()` (on pause / when leaving to the menu) and `load_position` when opening a book.

**Screen:** vertical scrollable list (LVGL list/flex, `LV_DIR_VER` scroll); each row = title (white 18 px) + author (gray 13 px) + `%` (right) + a 3 px red bar. Tap a row → show the loading screen → `load_book(path)` (or the sample) → `rsvp_open_book` → READER. Header label "LIBRARY"; BOOT → MENU.

---

## 6. Resume

- **Per-book position** = the current word index, saved to `/sdcard/.rsvp/<name>.pos` (4-byte LE). Written when: the user pauses, opens the menu (leaves the reader), or switches books — *not* every word (SD wear).
- On opening a book: if **resume-on-open** is enabled and a valid `.pos` exists (≤ wordCount), `seek()` to it; otherwise start at word 0.
- Library `%` is computed from `.pos` ÷ wordCount.

---

## 7. Settings (apply + persist)

`Settings` (in NVS, loaded at boot, applied immediately on change and saved):
- **Reading speed (WPM):** the persisted default; the reader's live swipe-up/down (±25) updates *this same value* and saves it. Settings stepper does the same (clamped 100–800).
- **Font size:** `Small | Medium | Large` → maps to enabled montserrat sizes (e.g. 36 / 48 / 64; enable the needed sizes in sdkconfig). Rebuilds the word labels' font.
- **Brightness:** `setUpduty()` (existing LEDC backlight); a level 1–5 → duty.
- **Leading / trailing words:** when off, `refresh_word()` skips the `g_prev`/`g_next` flanker labels.
- **Resume on open:** the §6 toggle.
- Rows for **Set clock** and **Est. time to finish** appear but are disabled/"soon" (filled by the RTC and estimated-time follow-ups).

**Screen:** scrollable list; numeric rows use − / + stepper buttons (~34 px targets), toggles use a pill switch (red = on). BOOT → MENU.

---

## 8. Touch fix

Set `mirror_y = 1` in the `esp_lcd_touch_config_t` (per the touch-bring-up note) so taps land correctly on the position-dependent menu/list/buttons. **Caveat:** this de-mirrors X and may flip the reader's left/right swipe — re-test and, if needed, swap the swipe-left/right mapping in `ui_reader` so sentence-skip stays correct.

---

## 9. Error handling

- **No SD / no books:** Library shows only the built-in "Sample" entry (always openable). A short "No books on card" hint above it.
- **Missing/again-removed book on open:** loading falls back to the sample; never bricks.
- **Corrupt/short `.pos` or `.idx` header:** treated as absent (start at 0 / show filename).
- **NVS load failure:** fall back to built-in defaults (WPM 300, Medium, brightness max, flankers on, resume on).

## 10. Testing

- **Host (TDD):** `CompiledIndex::readHeader` (title/author/wordCount from a serialized index, and graceful failure on short/garbage input) added to the doctest suite; the `%` computation is trivial and covered alongside.
- **On-device:** boot → Menu; Library lists books with author + %; open a book → resumes; change each setting → applies live and survives reboot; swipe-WPM persists; BOOT navigates every screen and toggles Reader↔Menu; taps land correctly; power-off still works from every screen.

## 11. Out of scope (follow-ups)
- RTC (PCF85063) bring-up + real clock display + the "Set clock" screen.
- Estimated time to finish (separate feature, to be specced).
- WiFi Drop implementation (WiFi AP + web upload) — the menu entry is a stub here.
- The BOOT button's other gestures (double/long press) — only short-press (menu/back) is used.

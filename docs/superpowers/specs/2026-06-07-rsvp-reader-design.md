# RSVP Reader — Design Spec

**Date:** 2026-06-07
**Status:** Approved (brainstorm complete) — ready for implementation planning
**Target hardware:** Waveshare **ESP32-S3-Touch-LCD-3.49**
**Firmware stack:** **ESP-IDF + LVGL**

---

## 1. Summary

A handheld **RSVP (Rapid Serial Visual Presentation) speed-reader**. Books and
documents live on a microSD card; the user browses them, opens one, and reads it
one word at a time at an adjustable pace with **ORP (Optimal Recognition Point)**
highlighting. The device monitors battery, controls screen brightness, auto-flips
the screen via the IMU, and accepts new documents over Wi-Fi without removing the
SD card.

The 640×172 wide bar display is a natural fit for RSVP: a single word held at a
fixed focal point with the ORP letter pinned to the center column.

**Definition of done (core product):** select an EPUB/TXT from the library, read
it with ORP + adjustable pacing and resume-on-reopen, configure the on-screen
look, control brightness, and upload a new book from a browser over Wi-Fi.

### Phasing headline
EPUB + plain-text first, fully **on-device**. PDF is a later, best-effort phase
(text-based PDFs, not scanned). Reading is **landscape-only**.

---

## 2. Decisions log (and rationale)

| Decision | Choice | Why |
|---|---|---|
| Firmware framework | **ESP-IDF + LVGL** | App is heavier than a demo (UI + Wi-Fi server + EPUB parse into PSRAM + FATFS); rewards IDF's control over PSRAM, partitions, networking, SD. Vendor ships an ESP-IDF + LVGL bring-up to fork. |
| Document pipeline | **Compile-and-cache** | Parse each book once into a compact index in a `/.rsvp/` sidecar; instant reopen/resume, bounded memory, and a clean indexer↔reader seam so PDF drops in later with no reader changes. |
| Format priority | **EPUB + TXT now, PDF later (best-effort)** | EPUB is a ZIP of XHTML — tractable on-device. Arbitrary PDF text extraction is hard; deferred and scoped to simple text PDFs. |
| Connectivity | **Wi-Fi upload only** (STA + first-run AP setup) | BLE realistically moves ~10–80 KB/s — minutes per multi-MB book. Wi-Fi is the right tool for file transfer. BLE dropped. |
| Settings scope | **Global settings** (one shared, configurable set) | Simplest to build and use; covers "device is configurable for whoever uses it." Named profiles are an explicit non-goal for v1. |
| Reading layout | **Word + faint flankers + focal lines + status**, all chrome toggleable | Chosen from mockups. Battery top-left, clock top-right, `wpm · %` bottom-center, vertical focal lines (no glow), no separate progress bar (the `%` covers it). |
| Settings panel | **Tile grid** of display toggles | Fast to scan, uses the wide bar, less scrolling than a list. |
| Library | **List** (cover thumb + title/author + `%`), swipe to scroll | Progress always visible; treats EPUB/PDF/TXT identically (no cover dependency). |

---

## 3. Hardware → feature map

| Peripheral | Part | Used for |
|---|---|---|
| MCU | ESP32-S3R8 (dual-core LX7, 8 MB PSRAM, 16 MB flash, Wi-Fi/BLE) | App + PSRAM buffers |
| Display + touch | AXS15231B (QSPI display, I²C touch) | 640×172 IPS render + capacitive input |
| IMU | QMI8658 (6-axis) | Auto 0°/180° flip from gravity vector |
| Power management IC + battery connector | (on-board) | Battery % + charge state |
| Backlight | LED via MCU PWM | **Brightness control (LEDC PWM)** |
| microSD / TF | FATFS | Books at root + `/.rsvp/` cache |
| Radio | ESP32-S3 Wi-Fi | STA + SoftAP upload server |

> **M0 confirmation items** (from the vendor schematic/demo): exact battery
> read path (ADC divider vs PMIC I²C), AXS15231B QSPI init sequence, flash
> partition sizing.

---

## 4. Architecture

```
┌─ App controller / screen state machine ───────────────────┐
│   LVGL UI:  Library │ Reader │ Settings │ Wi-Fi setup      │
└───┬───────────┬────────────────┬──────────────┬───────────┘
    │           │                │              │
 Catalog/    RSVP engine      Config store   Wi-Fi mgr +
 scanner    (ORP, pacing,     (NVS, global)  HTTP upload
    │        play/pause, nav)      │           server
    │           │                  │              │
┌───▼───────────▼──┐          ┌─────▼────┐   ┌─────▼─────┐
│ Compiled index   │          │ Progress │   │  SD root  │
│  (/.rsvp/*.idx)  │◄─────────│  store   │   │  (books)  │
└───▲──────────────┘          └──────────┘   └─────┬─────┘
    │ produces                                      │
┌───┴────────────────── Indexers ──────────────────▼──┐
│  EPUB (miniz + OPF + XHTML) │ TXT │ [PDF later]      │
└──────────────────────────────────────────────────────┘
```

**Central seam:** every indexer emits the **same uniform compiled token stream**
(words + structural flags). The RSVP engine consumes only that stream and never
learns the source format. Adding PDF later means writing one indexer and changing
nothing in the reader.

**Hardware independence:** the indexers and the RSVP engine are pure logic with no
hardware dependencies, so they compile and unit-test on a host machine.

### Module responsibilities & interfaces

- **Board/HAL** (forked from Waveshare bring-up): `display`, `touch`, `imu`,
  `power` (battery %, charge), `backlight` (set 0–100%), `sd` (mount FATFS).
  Each exposes a narrow C interface; the rest of the app never touches registers.
- **Indexer** (`index_source(path) -> writes /.rsvp/<id>.idx + .thumb`): per
  format. Input = raw file; output = compiled index + small cover thumbnail +
  extracted metadata. Pure/testable.
- **Catalog/scanner** (`scan() -> [BookEntry]`): walks SD root, reconciles against
  `/.rsvp/` (size+mtime), triggers (re)indexing, returns library entries
  (title, author, format, percent, thumb).
- **Stream provider** (`open(id) -> TokenCursor`): memory-light forward iterator
  over a compiled index with `seek(wordIndex)` via the sparse table.
- **RSVP engine** (`tick(now) -> RenderState`): pulls tokens, computes ORP pivot
  and per-word duration, handles play/pause and sentence/chapter navigation,
  emits what the Reader screen renders.
- **Config store** (NVS): global settings struct (display toggles, default WPM,
  brightness, orientation lock, theme, Wi-Fi creds). Load on boot, save on change.
- **Progress store** (`/.rsvp/state.bin`, keyed by book id): last word index +
  last-opened time. On SD so it travels with the card and survives reflash.
- **Wi-Fi manager**: STA join with saved creds; first-run SoftAP + captive portal
  for credential entry; mDNS (`rsvp.local`).
- **HTTP upload server** (`esp_http_server`): multipart upload streamed to SD root;
  triggers indexing on completion.
- **App controller**: LVGL screen state machine wiring the above together.

---

## 5. Data flow

1. A file lands on SD (Wi-Fi upload, or card inserted elsewhere).
2. **Scanner** detects new/changed entries (size + mtime vs index header).
3. **Indexer** parses once → writes `/.rsvp/<id>.idx` (tokens + chapter table +
   metadata) and `/.rsvp/<id>.thumb` (small RGB565 cover).
4. **Library** lists the book with progress.
5. On open, **stream provider** feeds tokens to the **engine**, which times and
   renders them on the **Reader** screen.
6. **Progress store** saves the current word offset periodically (e.g. every ~64
   words).

---

## 6. Compiled index format (`/.rsvp/<id>.idx`)

- **Header:** magic + version, source size + mtime (invalidation), title, author,
  word count, chapter count, cover reference.
- **Chapter table:** `{ wordOffset, title }` entries (labels + jump targets).
- **Sparse seek table:** byte offset at a fixed word interval (e.g. every 256
  words) for fast resume/jump.
- **Token stream:** per word `[flags:1][len:varint][utf8 bytes]`, where flags mark
  end-of-sentence / end-of-paragraph / chapter-start — these drive pacing.

Re-index when the source size/mtime changes or the format version bumps. Prune
orphaned `/.rsvp/` entries when a book is deleted.

---

## 7. SD card layout

```
/  Project Hail Mary.epub        ← books live at the root (per requirement)
   notes.txt
   document.pdf
   /.rsvp/                       ← hidden derived cache + state
       <id>.idx                  ← compiled token stream + metadata
       <id>.thumb                ← small cover thumbnail (RGB565)
       state.bin                 ← per-book progress + last-opened
```

---

## 8. Reading experience

### Reading screen (locked layout)
Dark screen. Center: the word with the **red ORP letter** pinned to the center
focal column, between short vertical **focal guide lines** above and below, with
faint **previous/next flankers** to the sides. Top: **battery (left)**, **clock
(right)**. Bottom-center: `wpm · %`. Every one of these chrome elements is
individually toggleable. Monospace word font keeps the ORP pivot stable.

### ORP algorithm
Pivot index by word length: 1–3 → 1st letter, 4–5 → 2nd, 6–9 → 3rd, 10+ → 4th.
Rendered as `pre | ORP | post` with the ORP letter centered on the focal column.

### Pacing *(proposed defaults — tunable)*
- Default **300 WPM**, range **100–1000**, step **25**.
- Base duration = `60000 / WPM` ms per word.
- Modifiers: long-word bonus; sentence-end pause (~2×); paragraph/chapter pause
  (larger); optional comma pause; optional slow-start ramp after un-pausing.

### Gestures *(proposed — tunable)*
- **Tap** → play/pause
- **Swipe ↑ / ↓** → WPM ±
- **Swipe ← / →** → back / forward one sentence
- **Long-press** → exit to library
- Onboard GPIO0 button optionally mirrors play/pause.

### Orientation
Landscape-only. Auto **0°/180°** flip from the accelerometer's gravity vector
(debounced to avoid flicker), so text is upright whichever way the device is held.
**Manual orientation lock** available in settings.

---

## 9. Settings (global, stored in NVS)

- **Display tile-grid** (chosen UI): toggles for flankers, battery, clock,
  `wpm·%`, theme, etc.
- **Full settings menu:**
  - **Screen brightness** — backlight PWM slider (0–100%), persisted in NVS and
    re-applied on boot. *(First-class control.)*
  - **Default WPM** — starting pace for new sessions.
  - **Orientation lock** — auto-flip on/off.
  - **Wi-Fi setup** — credential entry / status / device address.
  - **About** — firmware version, storage, battery.

Reading **progress** lives on the SD card (`/.rsvp/state.bin`), not NVS, so it
travels with the card.

---

## 10. Connectivity (Wi-Fi, STA + AP setup)

- **First run / no credentials:** device hosts a `RSVP-Reader-setup` SoftAP with a
  captive portal; the user enters home SSID/password from a phone; creds saved to
  NVS; device reconnects as STA.
- **Normal:** joins saved network; **Settings → Wi-Fi** shows the device address /
  `rsvp.local` (mDNS).
- **Upload:** open that address in any browser → drag-drop EPUB/PDF/TXT →
  multipart stream written to SD root → auto-indexed → appears in library.
- **Security:** LAN-only, no auth by default; an optional upload PIN is noted as a
  future hardening step.

---

## 11. Error handling & edge cases

- **Corrupt/unsupported file:** indexer marks it failed; library shows an error
  state; no crash.
- **Very large book:** bounded memory via streaming + sparse seek table; indexing
  shows progress.
- **SD absent/removed:** friendly retry screen.
- **Low battery:** warn + dim; **critical:** save progress and sleep.
- **Power loss:** progress saved periodically (e.g. every ~64 words), so little
  is lost.
- **Cache invalidation:** on source size/mtime change or index version bump.

---

## 12. Testing strategy

- **Host unit tests (primary value layer):** tokenizer, ORP pivot, pacing math,
  EPUB tag-stripping + HTML-entity decode, index serialize/deserialize,
  cache-invalidation. All hardware-independent. Commit EPUB/TXT fixtures including
  awkward cases (nested markup, entities, big files, missing metadata).
- **On-device integration (semi-manual):** SD I/O, LVGL render, touch gestures,
  Wi-Fi upload round-trip, IMU auto-flip, battery/brightness.

---

## 13. Build order (each milestone independently demoable)

| Milestone | Deliverable |
|---|---|
| **M0 Bring-up** | Fork Waveshare ESP-IDF demo; verify display/touch/IMU/SD/battery/backlight. |
| **M1 RSVP core** | Hard-coded token stream → reading screen with ORP, pacing, gestures, play/pause, WPM. *(The heart.)* |
| **M2 TXT + library + cache + settings** | Full pipeline end-to-end on TXT; library list; resume; brightness; orientation flip; global settings store. |
| **M3 EPUB indexer** | miniz unzip + OPF spine + XHTML strip → metadata/cover → same pipeline. |
| **M4 Settings UI** | Tile-grid display toggles + full settings menu (incl. brightness, WPM, orientation, about). |
| **M5 Wi-Fi upload** | STA + AP setup, captive portal, `esp_http_server` upload, mDNS, auto-index on upload. |
| **M6 PDF (stretch)** | Best-effort text extraction for simple text PDFs (new indexer only). |

---

## 14. Non-goals (v1)

Scanned-PDF OCR; complex/CID-font PDF extraction; BLE; named multi-user profiles;
CJK/RTL fonts; audio features (the board's codec/mics are unused); cloud sync.

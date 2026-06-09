# RSVP Reader — SD Card Mount + EPUB/TXT Loading — Design Spec

**Date:** 2026-06-09
**Status:** Approved (brainstorm complete) — ready for implementation planning
**Target hardware:** Waveshare **ESP32-S3-Touch-LCD-3.49** (SDMMC TF slot, octal PSRAM)
**Firmware stack:** ESP-IDF 5.5.2 + LVGL 9; host-tested C++17 `core` engine

---

## 1. Summary

Replace the hard-coded sample text in the reader with a **real book loaded from the
microSD card**. On boot the firmware mounts the SD card, finds the first EPUB/TXT on
it, compiles it once into the existing compiled-index format (caching the result to a
`/.rsvp/` sidecar so reopens are instant), and feeds the resulting `Document` to the
`Player`. If anything is missing or unreadable, it falls back to the built-in sample
so the device always boots into something.

This is **sub-project A** of Phase 1. The power button is sub-project B (separate
spec). Choosing a book from a list, settings, and resume-on-reopen are **Phase 2**.

**Definition of done:** drop a real `.epub` (or `.txt`) on a FAT32 card, power on →
the device reads that book (correct title/author shown) at the set WPM with working
gestures; a second boot loads from the cache (logged "cache hit"); an empty/corrupt
card boots into the sample.

---

## 2. Hardware facts (from `waveshareteam/ESP32-S3-Touch-LCD-3.49`, `Examples/ESP-IDF/04_SD_Card`)

- **The SD card is SDMMC, 1-line — NOT SPI.** The `SDSPI_HOST SPI2_HOST` define in
  `firmware/main/user_config.h` is dead leftover from the LCD demo; SD uses the SDMMC
  peripheral.
- Pins: **CLK = GPIO41, CMD = GPIO39, D0 = GPIO40**. Mount with
  `esp_vfs_fat_sdmmc_mount("/sdcard", …)`, `SDMMC_HOST_DEFAULT()` at
  `SDMMC_FREQ_HIGHSPEED`, `SDMMC_SLOT_CONFIG_DEFAULT()` with `width = 1`,
  `format_if_mount_failed = false`. FAT32 cards.
- `sdkconfig.defaults` already enables FATFS (`CONFIG_FATFS_LFN_HEAP`,
  `CONFIG_FATFS_SECTOR_512`); the SDMMC host driver ships with ESP-IDF.

---

## 3. Decisions log (and rationale)

| Decision | Choice | Why |
|---|---|---|
| Book selection (Phase 1) | **First `*.epub`/`*.txt` found in `/sdcard`** (top level, case-insensitive, skipping `.rsvp/`) | No UI yet; "drop a book on the card and it reads." The scan is reused by the Phase-2 library. |
| Caching | **Compile-and-cache now** to `/sdcard/.rsvp/<name>.idx`, gated by `indexMatchesSource` | Infra already exists (`epubToIndex` returns cacheable bytes); makes reopen instant and matches the product spec's headline decision. |
| Formats | **EPUB + TXT** | EPUB via `epubToIndex`; TXT via `tokenizePlainText` + `serializeIndex` — both yield the same compiled-index bytes, so TXT is nearly free. |
| Resume-on-reopen | **Deferred to Phase 2** | Per-book position state is managed with the library/settings; keep this slice to mount→parse→cache→read. |
| Loader placement | **Thin firmware `book_loader` over host-tested `core`** | Heavy lifting (zip/parse/serialize/index) is already covered by host tests; only file-I/O + the cache decision are new glue. |
| New pure unit | **`cacheIndexPath` in `core`, host-tested** | The one piece of new non-glue logic worth isolating and testing. |
| Whole-file load | **Read the whole EPUB into PSRAM** (no streaming) | ZIP needs random access; 8 MB PSRAM holds a typical book. A size cap guards OOM. |

---

## 4. Components

### 4.1 `core/` additions
- `core/include/rsvp/cachepath.hpp` + `core/src/cachepath.cpp` — pure:
  ```cpp
  namespace rsvp {
  // "/sdcard/Foo.epub" -> "/sdcard/.rsvp/Foo.epub.idx". Keeps the full filename
  // (incl. extension) so Foo.epub and Foo.txt don't collide. Deterministic.
  std::string cacheIndexPath(const std::string& bookPath);
  }
  ```
- Register the so-far-host-only sources for the **firmware** build by adding to
  `core/CMakeLists.txt` `SRCS`: `index/entity/htmltext/opf/epub/zipreader` (+ the new
  `cachepath`). Add the new files to `test/host/CMakeLists.txt` + a `test_cachepath.cpp`.

### 4.2 `firmware/components/miniz/`
Thin ESP-IDF component wrapping `third_party/miniz/miniz.c` (compiles `miniz.c`,
exposes `third_party/miniz` as an include dir), so the firmware `zipreader` links.

### 4.3 `firmware/components/sdcard_bsp/`
`sdcard_init()` (SDMMC mount per §2) + a `bool sdcard_read_file(path, std::vector<uint8_t>&)`
helper and a `sdcard_write_file(path, bytes)` helper. Adapted from the Waveshare
example; C linkage so `main.c` can call `sdcard_init()`.

### 4.4 `firmware/main/book_loader.{hpp,cpp}` (C++)
```cpp
namespace rsvp { struct LoadedBook { Document doc; std::string title, author; }; }
std::optional<rsvp::LoadedBook> load_first_book();   // nullopt on no-card/no-book/failure
```
Flow:
1. Open `/sdcard`; pick the first `.epub`/`.txt` (skip `.rsvp/`, hidden entries).
2. Read its bytes; `stat()` for size + mtime.
3. `path = cacheIndexPath(bookPath)`. If the cache exists and
   `indexMatchesSource(cacheBytes, size, mtime)` → use cacheBytes (log "cache hit").
4. Else compile: EPUB → `epubToIndex(bytes, size, mtime)`; TXT →
   `serializeIndex(tokenizePlainText(text), meta, {})`, where `meta.sourceSize/Mtime`
   are set (so the cache validates on reopen) and `meta.title` defaults to the filename
   stem with `meta.author` empty (TXT carries no metadata). `mkdir /sdcard/.rsvp`,
   write the `.idx`.
5. `auto ci = CompiledIndex::parse(idxBytes)`; on `ci.ok()` return
   `{ ci.toDocument(), ci.meta().title, ci.meta().author }` — the parsed index's
   `meta()` covers both EPUB (from the OPF) and TXT (the filename stem).

### 4.5 `firmware/main/` integration
`app_main` calls `sdcard_init()` before screen creation. `rsvp_reading_screen_create`
gains a `Document` parameter (plus optional title/author); `app_main` passes
`load_first_book()`’s result or the built-in sample on `nullopt`. The top status labels
show `title`/`author` instead of the `"84%"/"2:14"` placeholders when a book loaded.

---

## 5. Data flow

```
boot → sdcard_init() (SDMMC /sdcard)
     → load_first_book(): scan → read bytes+stat → cacheIndexPath
          → cache valid? load .idx : compile (epubToIndex | tokenize+serialize) → write .idx
          → CompiledIndex::parse → toDocument
     → rsvp_reading_screen_create(doc | sample) → Player → existing ORP/pacing/gesture path
```

## 6. Error handling
- Mount fail / no card / no book / bad ZIP / parse fail / file > size cap → log reason,
  return `nullopt`, reader runs the built-in sample. The device never bricks on bad input.
- All large buffers (`epub bytes`, `idx bytes`, token stream) in PSRAM via `heap_caps`;
  skip files larger than a configurable cap (~8 MB) before reading.
- Cache write failure is non-fatal: parse the freshly-compiled in-memory index anyway.

## 7. Testing
- **Host:** new `test/host/test_cachepath.cpp` (basic path, `.txt` vs `.epub` no-collision,
  nested/no-extension/edge inputs). Existing pipeline tests already cover
  `epubToIndex`/`serializeIndex`/`CompiledIndex`/`indexMatchesSource`.
- **On-device:** (1) real `.epub` on card → boots into it, title/author correct, gestures
  work; (2) reboot → serial shows "cache hit", `.rsvp/<name>.idx` present; (3) `.txt` file
  → reads; (4) empty card / no SD / truncated EPUB → sample fallback, logged.

## 8. Out of scope (separate specs / phases)
- **Power button / shutdown** — sub-project B (next spec).
- **Library/browse UI, settings, resume-on-reopen** — Phase 2.
- Recursive directory scan, multi-book management, PDF — later.

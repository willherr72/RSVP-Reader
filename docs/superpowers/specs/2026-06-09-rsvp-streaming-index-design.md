# RSVP Reader — Bounded-Memory Book Loading (Streaming Index + On-Demand Reader) — Design Spec

**Date:** 2026-06-09
**Status:** Approved (brainstorm complete) — ready for implementation planning
**Target hardware:** Waveshare **ESP32-S3-Touch-LCD-3.49** (8 MB PSRAM)
**Firmware stack:** ESP-IDF 5.5.2 + LVGL 9; host-tested C++17 `core` engine

---

## 1. Summary

A full-length novel currently **crashes the device** (boot loop) when loaded from SD.
The fix makes book loading use **bounded memory regardless of book size**, by never
materializing the whole book as an in-RAM `Document`:

1. **Stream the compile** — serialize tokens into the compact index as each chapter is
   tokenized, holding only the ~1 MB serialized stream, not the ~5 MB `Document`.
2. **Read on demand** — the `Player` and reader pull the current/previous/next words
   from the compact `CompiledIndex` via `at(i)`, instead of a materialized `Document`.
3. **Loading screen** — the compile runs in a dedicated FreeRTOS task; LVGL shows a
   "Loading…" screen and swaps to the reader when the task signals done.

**Definition of done:** Project Hail Mary (1.27 MB EPUB, 149,440 words) loads from SD
(Loading → reader), reads with working gestures, and caches for instant reopen; peak
PSRAM during load and read stays ~1–2 MB (independent of book size); host tests stay
green; the built-in sample still works as a fallback.

---

## 2. Root cause (confirmed on-device)

A 149,440-word book builds a ~5 MB in-memory `Document` (`std::vector<Token>`, each
`Token` a `std::string`) inside `epubToIndex` before serializing. With `std::vector`
growth needing ~1.5–2× transiently, the peak (~7.5 MB) overruns the **6.4 MB free
PSRAM** (8 MB − LVGL framebuffers − reservations). `bad_alloc` under `-fno-exceptions`
→ `terminate` → `BREAK`/boot-loop. Measured: after reading the EPUB, `free PSRAM = 6.4 MB,
largest block = 6.0 MB`, then the crash inside `epubToIndex`. The parser itself is
correct (host: same EPUB → 1.16 MB index, 149,440 words, title/author parsed).

The product design already intends a **compact cached index** read by streaming — the
current code defeats it by materializing the full `Document` both at compile time
(`epubToIndex`) and read time (`CompiledIndex::toDocument()`).

---

## 3. Decisions log (and rationale)

| Decision | Choice | Why |
|---|---|---|
| Read side | **Player + reader read from `CompiledIndex.at(i)`** (no `Document`) | One read path; matches "everything is a compact index". The sample compiles to a tiny index too. |
| `Player` API | **`Player(const CompiledIndex&, cfg)`** (was `const Document&`) | Direct; avoids a `TokenSource` abstraction for only two consumers (YAGNI). Tests build a small index. |
| Compile side | **Incremental `IndexBuilder`** fed per chapter | Holds only the ~1 MB serialized stream + seek table, never the ~5 MB `Document`. |
| Back-compat | **Keep `serializeIndex` / `tokenizePlainText` / `toDocument`** | Reimplement `serializeIndex` on `IndexBuilder`; keep the Document wrappers so existing host tests stay valid. Byte-identical output. |
| Loading UX | **Compile in a dedicated task; "Loading…" screen; `lv_timer` transition** | Keeps LVGL single-threaded; gives a big stack for the parser; non-blocking first open. |
| Failure | **Compile failure → sample index** | Device always lands in the reader (never bricks). |

---

## 4. Components

### 4.1 `core` — `IndexBuilder` (new, host-tested)
`core/include/rsvp/indexbuilder.hpp` + `core/src/indexbuilder.cpp`:
```cpp
class IndexBuilder {
public:
    explicit IndexBuilder(const DocMeta& meta, std::uint32_t seekInterval = kDefaultSeekInterval);
    void startChapter(const std::string& title = "");      // marks a chapter at the current word
    void addToken(const std::string& text, std::uint8_t flags);
    std::vector<std::uint8_t> finish();                    // header + chapters + seek table + stream
private:
    DocMeta meta_; std::uint32_t seekInterval_; std::uint32_t wordCount_ = 0;
    std::vector<std::uint8_t> tokenStream_; std::vector<std::uint32_t> seekOffsets_;
    std::vector<Chapter> chapters_;
};
```
- `addToken`: at each `seekInterval` boundary record `tokenStream_.size()`; append `flags` + `blob16(text)`; `++wordCount_`. No `Token` objects retained.
- `finish`: emit the exact existing format (magic/ver/flags/size/mtime/wordCount/chapterCount/seekInterval, title, author, chapters, seek table, token stream).
- `serializeIndex(doc, meta, chapters, …)` is reimplemented as: build an `IndexBuilder`, replay `chapters` + all `doc.tokens`, `finish()`. Same bytes.

### 4.2 `core` — streaming tokenizer
`core/src/tokenize.cpp`: add `void tokenizePlainTextInto(const std::string& text,
const std::function<void(const std::string&, std::uint8_t)>& sink)` carrying the existing
word/sentence/paragraph-flag logic. `tokenizePlainText(text)` becomes a wrapper that
collects into a `Document` (unchanged behavior, existing tests valid).

### 4.3 `core` — `epubToIndex` refactor
`core/src/epub.cpp`: build an `IndexBuilder`; for each spine href: read+decompress the
chapter (transient), `htmlToText` (transient), `startChapter()`, then
`tokenizePlainTextInto(chapterText, [&](w,f){ ib.addToken(w, f | chapterStartFlag); })`,
freeing the chapter buffers before the next. `ib.finish()`. FLAG_CHAPTER_START applies to
the first token of the 2nd+ non-empty chapter (as today). Output byte-identical to the
current implementation (verified by existing `test_epub`).

### 4.4 `core` — `Player` reads from `CompiledIndex`
`player.hpp`/`player.cpp`: `Player(const CompiledIndex& idx, PacingConfig cfg)`. Replace
`doc_.tokens[index_]`/`doc_.size()` with a cached `current_` token (re-materialized from
`idx_.at(index_)` on each index change) and `idx_.wordCount()`. `prevSentence`/
`nextSentence` scan with `idx_.at()`. `current()` returns the cached token.

### 4.5 `firmware` — Loading screen + compile task + transition
- `main.c`/`ui_reader.cpp`: after `sdcard_init`, build a minimal **Loading screen**
  (centered "Loading…") and start the LVGL task. Spawn a **compile task** (≈32 KB stack,
  core 1) that runs `load_first_book()` → a file-scope result `{ CompiledIndex index; std::string title; }`
  (the sample-compiled index on any failure), then sets an atomic `g_load_done`.
- An `lv_timer` on the LVGL thread polls `g_load_done`; when set, it builds the reader
  screen from the index, deletes the Loading screen, and stops itself. The task never
  calls LVGL.
- `book_loader`: `load_first_book()` now returns the **`CompiledIndex`** (+ title) rather
  than a `Document`; on no-card/no-book/failure it returns the **sample** compiled index.
- `refresh_word()`/reader: render from `g_index.at(idx-1/idx/idx+1)`.

---

## 5. Data flow

```
boot → sdcard_init → Loading screen + LVGL task started
     → compile task (big stack, core 1):
         load_first_book(): scan → cache hit? parse(.idx) : stream-compile(epubToIndex) + write .idx
                            → CompiledIndex (or sample index)
         store result; g_load_done = true
     → lv_timer (LVGL thread): g_load_done -> build reader from CompiledIndex, drop Loading screen
reader: Player advances index over time; refresh_word pulls at(i-1), at(i), at(i+1)
peak PSRAM: compile ~1.2 MB (stream + transient chapter), read ~1.2 MB (index) — size-independent
```

## 6. Error handling & memory
- Any failure (mount/scan/read/compile/parse/size-cap) → the compile task substitutes the
  **sample** index, so the reader always appears. Logged.
- Large buffers in PSRAM (`heap_caps`/`std::vector` >16 KB auto-PSRAM). Source size cap
  (4 MB) retained. The compile task stack (~32 KB internal) covers the parser depth.
- Peak target: load and read each ≤ ~2 MB PSRAM, independent of book size.

## 7. Testing
- **Host:** `IndexBuilder` golden test — byte-identical to current `serializeIndex` for a
  sample doc + chapters; `tokenizePlainTextInto` emits the same tokens/flags as
  `tokenizePlainText`; existing `test_epub`/`test_index` unchanged (output identical);
  `test_player` ported to construct the `Player` from a `CompiledIndex` (build a small
  index, same assertions).
- **On-device:** Project Hail Mary → Loading → reader, reads at WPM, gestures work; reboot
  → cache hit (instant, no Loading delay); empty/no card → sample; peak PSRAM logged
  during bring-up confirms ~1–2 MB.

## 8. Out of scope
- Streaming the SD read itself (whole EPUB into PSRAM is fine at ≤4 MB).
- Resume-on-reopen, library/browse/settings UI (Phase 2).
- A progress bar/percentage on the Loading screen (a static "Loading…" suffices).
- Power button (sub-project B).

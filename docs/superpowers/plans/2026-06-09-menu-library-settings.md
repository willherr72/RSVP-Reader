# Menu + Library + Settings (Phase 2A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A BOOT-button menu with Library (open any SD book, resume per book) and Settings (persisted reader options).

**Architecture:** Split the UI into `ui_reader` (reader) + `ui_menu` (menu/library/settings + nav state machine); add `app_settings` (NVS) and extend `book_loader` (list/load/position). One host-tested core helper (`readIndexHeader`); everything else is firmware glue verified on-device. The reader stays the boot screen until the final task flips boot→menu, so each task is independently testable.

**Tech Stack:** ESP-IDF 5.5.2 (`nvs_flash`), LVGL 9, C++17 `core` (doctest).

---

## Conventions

**[HOST]**: `cmake --build "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\build\host" --config Debug` then `& "...\build\host\Debug\rsvp_tests.exe"`
**[BUILD]/[FLASH]**: `$idf="C:\Users\WilliamHerr\esp\v5.5.2\esp-idf"; $env:IDF_PATH=$idf; $env:IDF_PYTHON_ENV_PATH="C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env"; & "$idf\export.ps1" *> $null; idf.py -C "...\firmware" build` (flash: `-p COM32 flash`).
**[CAPTURE n]**: `& "...\idf5.5_py3.11_env\Scripts\python.exe" "...\firmware\build\cap.py" n`

**Visual spec:** approved mockups in `.superpowers/brainstorm/30247-1781033036/content/` (`menu-layout.html`, `library-v2.html`, `settings.html`). Colors: bg `#000`, tile/row `#15171c`, text `#f2f5fa`, dim `#8893a6`, accent/red `#ff3b3b`, stepper border `#4a525f`.

**Index byte format (from `core/src/index.cpp`):** magic(4)`RSVI` · version(2)=1 · flags(2) · sourceSize(4) · sourceMtime(4) · **wordCount(4)** · chapterCount(4) · seekInterval(4) · **title**(u16 len + bytes) · **author**(u16 len + bytes) · chapters · seek table · token stream.

---

## Task 1: `readIndexHeader` (core, host-TDD)

**Files:** Modify `core/include/rsvp/index.hpp`, `core/src/index.cpp`, `test/host/test_index.cpp`.

- [ ] **Step 1: Declare** — in `core/include/rsvp/index.hpp`, before `} // namespace rsvp`:
```cpp
// Lightweight metadata read for fast library listing: parses ONLY the fixed header
// (title, author, word count) without decoding the token stream. ok=false on bad
// magic/version/truncation.
struct IndexHeader {
    std::string   title;
    std::string   author;
    std::uint32_t wordCount = 0;
    bool          ok = false;
};
IndexHeader readIndexHeader(const std::vector<std::uint8_t>& bytes);
```

- [ ] **Step 2: Failing test** — append to `test/host/test_index.cpp`:
```cpp
TEST_CASE("readIndexHeader pulls title/author/wordCount without full parse") {
    Document doc; doc.tokens = { Token{"a",FLAG_NONE}, Token{"b",FLAG_NONE}, Token{"c",FLAG_NONE} };
    DocMeta m; m.title = "Project Hail Mary"; m.author = "Andy Weir";
    auto bytes = serializeIndex(doc, m, {});

    IndexHeader h = readIndexHeader(bytes);
    REQUIRE(h.ok);
    CHECK(h.title == "Project Hail Mary");
    CHECK(h.author == "Andy Weir");
    CHECK(h.wordCount == 3);
}

TEST_CASE("readIndexHeader fails cleanly on short/garbage input") {
    CHECK_FALSE(readIndexHeader({}).ok);
    CHECK_FALSE(readIndexHeader({'R','S','V','I'}).ok);
    CHECK_FALSE(readIndexHeader(std::vector<std::uint8_t>(10, 0xFF)).ok);
}
```

- [ ] **Step 3: Run, expect red** — [HOST]: link error / failure (`readIndexHeader` undefined).

- [ ] **Step 4: Implement** — in `core/src/index.cpp`, after `indexMatchesSource`:
```cpp
IndexHeader readIndexHeader(const std::vector<std::uint8_t>& b) {
    IndexHeader h;
    std::size_t off = 0;
    auto need = [&](std::size_t k) { return off + k <= b.size(); };
    if (!need(4) || b[0]!='R' || b[1]!='S' || b[2]!='V' || b[3]!='I') return h;
    off = 4;
    if (!need(2) || getU16(b, off) != kVersion) return h;   // version (advances off)
    if (!need(2)) return h; getU16(b, off);                 // flags
    if (!need(4)) return h; getU32(b, off);                 // sourceSize
    if (!need(4)) return h; getU32(b, off);                 // sourceMtime
    if (!need(4)) return h; h.wordCount = getU32(b, off);
    if (!need(4)) return h; getU32(b, off);                 // chapterCount
    if (!need(4)) return h; getU32(b, off);                 // seekInterval
    if (!need(2)) return h; const std::uint16_t tl = getU16(b, off);
    if (!need(tl)) return h; h.title.assign(b.begin()+off, b.begin()+off+tl); off += tl;
    if (!need(2)) return h; const std::uint16_t al = getU16(b, off);
    if (!need(al)) return h; h.author.assign(b.begin()+off, b.begin()+off+al); off += al;
    h.ok = true;
    return h;
}
```

- [ ] **Step 5: Run, expect green** — [HOST]: all pass.

- [ ] **Step 6: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add core/include/rsvp/index.hpp core/src/index.cpp test/host/test_index.cpp; git commit -m "feat(core): readIndexHeader for fast library metadata"
```

---

## Task 2: `app_settings` (NVS)

**Files:** Create `firmware/main/app_settings.h`, `firmware/main/app_settings.cpp`; Modify `firmware/main/CMakeLists.txt` (if it lists sources explicitly).

- [ ] **Step 1: Header** — `firmware/main/app_settings.h`:
```cpp
#pragma once
#include <cstdint>
#ifdef __cplusplus
extern "C" { struct app_settings_dummy_; }   // (no C users; header is C++-only)
#endif

enum FontSize { FONT_SMALL = 0, FONT_MEDIUM = 1, FONT_LARGE = 2 };

struct Settings {
    int      wpm          = 300;     // 100..800
    FontSize font         = FONT_MEDIUM;
    int      brightness   = 5;       // 1..5
    bool     show_flankers = true;   // leading/trailing words
    bool     resume_on_open = true;
};

Settings& settings();        // the single in-RAM instance
void settings_load();        // from NVS (namespace "rsvp"); defaults if missing
void settings_save();        // persist current settings()
```

- [ ] **Step 2: Implementation** — `firmware/main/app_settings.cpp`:
```cpp
#include "app_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char* TAG = "settings";
static const char* NS  = "rsvp";
static Settings s_settings;

Settings& settings() { return s_settings; }

void settings_load()
{
    static bool nvs_ready = false;
    if (!nvs_ready) {
        esp_err_t e = nvs_flash_init();
        if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase(); nvs_flash_init();
        }
        nvs_ready = true;
    }
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) { ESP_LOGI(TAG, "no saved settings; defaults"); return; }
    int32_t v;
    if (nvs_get_i32(h, "wpm",  &v) == ESP_OK) s_settings.wpm = v;
    if (nvs_get_i32(h, "font", &v) == ESP_OK) s_settings.font = (FontSize)v;
    if (nvs_get_i32(h, "bri",  &v) == ESP_OK) s_settings.brightness = v;
    uint8_t b;
    if (nvs_get_u8(h, "flank", &b) == ESP_OK) s_settings.show_flankers = b;
    if (nvs_get_u8(h, "resume",&b) == ESP_OK) s_settings.resume_on_open = b;
    nvs_close(h);
    ESP_LOGI(TAG, "loaded: wpm=%d font=%d bri=%d flank=%d resume=%d",
             s_settings.wpm, s_settings.font, s_settings.brightness,
             s_settings.show_flankers, s_settings.resume_on_open);
}

void settings_save()
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) { ESP_LOGW(TAG, "save: nvs_open failed"); return; }
    nvs_set_i32(h, "wpm",  s_settings.wpm);
    nvs_set_i32(h, "font", s_settings.font);
    nvs_set_i32(h, "bri",  s_settings.brightness);
    nvs_set_u8 (h, "flank", s_settings.show_flankers);
    nvs_set_u8 (h, "resume",s_settings.resume_on_open);
    nvs_commit(h);
    nvs_close(h);
}
```
(`firmware/main/CMakeLists.txt` uses a glob/`SRCS` — if explicit, add `app_settings.cpp` and `REQUIRES nvs_flash`. Check it; ESP-IDF main usually globs.)

- [ ] **Step 3: Smoke-wire** — in `firmware/main/main.c`, add `#include "app_settings.h"`? No — it's C++. Instead call `settings_load()` from `ui_reader`/`ui_menu` later. For now just build.

- [ ] **Step 4: Build** — [BUILD]: compiles (links `nvs_flash`).

- [ ] **Step 5: Commit**
```powershell
git add firmware/main/app_settings.h firmware/main/app_settings.cpp firmware/main/CMakeLists.txt; git commit -m "feat(firmware): app_settings persisted to NVS"
```

---

## Task 3: `book_loader` — list, load-by-path, position

**Files:** Modify `firmware/main/book_loader.hpp`, `firmware/main/book_loader.cpp`.

- [ ] **Step 1: Header** — replace `firmware/main/book_loader.hpp` body (keep `LoadedBook`):
```cpp
#pragma once
#include "rsvp/index.hpp"
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

struct LoadedBook { rsvp::CompiledIndex index; std::string title; };

struct BookEntry {
    std::string   path;       // "/sdcard/Foo.epub"
    std::string   title;      // from .idx header, else filename
    std::string   author;     // from .idx header, else ""
    std::uint32_t wordCount = 0;
    std::uint32_t position  = 0;   // from .pos, else 0
};

// All books on the card (+ a built-in "Sample" entry, path == "" ).
std::vector<BookEntry> list_books();
// Load (compile+cache if needed) + parse a specific book. path=="" -> the sample.
std::optional<LoadedBook> load_book(const std::string& path);

void          save_position(const std::string& bookPath, std::uint32_t wordIndex);
std::uint32_t load_position(const std::string& bookPath);   // 0 if none/invalid
```

- [ ] **Step 2: Refactor `book_loader.cpp`** — generalize the existing single-book code. Add includes `#include "rsvp/cachepath.hpp"` (already there) and helpers; the compile/cache body of the old `load_first_book` becomes `load_book(path)`:
```cpp
// (keep ends_with_ci, read_file, filename_stem, kMaxBookBytes)

static std::string pos_path(const std::string& bookPath) {
    return rsvp::cacheIndexPath(bookPath) + ".pos";   // /sdcard/.rsvp/<name>.epub.idx.pos
}

void save_position(const std::string& bookPath, std::uint32_t idx) {
    if (bookPath.empty()) return;
    mkdir("/sdcard/.rsvp", 0777);
    FILE* f = fopen(pos_path(bookPath).c_str(), "wb");
    if (f) { fwrite(&idx, 1, sizeof idx, f); fclose(f); }
}
std::uint32_t load_position(const std::string& bookPath) {
    if (bookPath.empty()) return 0;
    FILE* f = fopen(pos_path(bookPath).c_str(), "rb");
    if (!f) return 0;
    std::uint32_t idx = 0;
    size_t got = fread(&idx, 1, sizeof idx, f);
    fclose(f);
    return got == sizeof idx ? idx : 0;
}

static rsvp::CompiledIndex compile_sample() {
    rsvp::IndexBuilder ib(rsvp::DocMeta{});
    rsvp::tokenizePlainTextInto(
        "Rapid serial visual presentation shows one word at a time. "
        "Your eyes stay still while the words flow past you. "
        "This little reader is now alive on the hardware!",
        [&](const std::string& w, std::uint8_t f){ ib.addToken(w, f); });
    return rsvp::CompiledIndex::parse(ib.finish());
}

std::optional<LoadedBook> load_book(const std::string& book) {
    if (book.empty()) {                       // built-in sample
        auto ci = compile_sample();
        return LoadedBook{ std::move(ci), "Sample" };
    }
    if (!sdcard_mounted()) return std::nullopt;
    struct stat st;
    if (stat(book.c_str(), &st) != 0) return std::nullopt;
    const std::uint32_t size = (std::uint32_t)st.st_size, mtime = (std::uint32_t)st.st_mtime;
    const std::string idxPath = rsvp::cacheIndexPath(book);

    std::vector<std::uint8_t> idx, cached;
    if (read_file(idxPath, cached) && rsvp::indexMatchesSource(cached, size, mtime)) {
        ESP_LOGI(TAG, "cache hit: %s", idxPath.c_str());
        idx = std::move(cached);
    } else {
        std::vector<std::uint8_t> raw;
        if (!read_file(book, raw)) { ESP_LOGW(TAG, "read failed"); return std::nullopt; }
        ESP_LOGI(TAG, "compiling %s (%u bytes)...", book.c_str(), (unsigned)raw.size());
        if (ends_with_ci(book, ".epub")) idx = rsvp::epubToIndex(raw, size, mtime);
        else {
            rsvp::DocMeta meta; meta.title = filename_stem(book);
            meta.sourceSize = size; meta.sourceMtime = mtime;
            std::string text((const char*)raw.data(), raw.size());
            idx = rsvp::serializeIndex(rsvp::tokenizePlainText(text), meta, {});
        }
        if (idx.empty()) { ESP_LOGW(TAG, "compile failed"); return std::nullopt; }
        mkdir("/sdcard/.rsvp", 0777);
        FILE* f = fopen(idxPath.c_str(), "wb");
        if (f) { fwrite(idx.data(), 1, idx.size(), f); fclose(f); }
    }
    rsvp::CompiledIndex ci = rsvp::CompiledIndex::parse(idx);
    if (!ci.ok()) return std::nullopt;
    std::string title = ci.meta().title;
    ESP_LOGI(TAG, "loaded \"%s\", %u words", title.c_str(), (unsigned)ci.wordCount());
    return LoadedBook{ std::move(ci), std::move(title) };
}

std::vector<BookEntry> list_books() {
    std::vector<BookEntry> out;
    DIR* d = sdcard_mounted() ? opendir("/sdcard") : nullptr;
    if (d) {
        for (dirent* e = readdir(d); e; e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            std::string name = e->d_name;
            if (!ends_with_ci(name, ".epub") && !ends_with_ci(name, ".txt")) continue;
            BookEntry be; be.path = "/sdcard/" + name; be.title = name;
            std::vector<std::uint8_t> cached;
            if (read_file(rsvp::cacheIndexPath(be.path), cached)) {
                rsvp::IndexHeader h = rsvp::readIndexHeader(cached);
                if (h.ok) { be.title = h.title.empty() ? name : h.title; be.author = h.author; be.wordCount = h.wordCount; }
            }
            be.position = load_position(be.path);
            out.push_back(std::move(be));
        }
        closedir(d);
    }
    out.push_back(BookEntry{ "", "Sample", "built-in", 0, 0 });   // always openable
    return out;
}
```
Add `#include "rsvp/indexbuilder.hpp"` and `#include "rsvp/tokenize.hpp"`. Keep `load_first_book()` as `return load_book(find_first_book());` (so the reader still boots until Task 9).

- [ ] **Step 3: Build + flash + verify** — [BUILD], [FLASH], [CAPTURE 8]: reader still boots/loads (regression check). Add a temporary `for (auto& b : list_books()) ESP_LOGI(...)` in `book_loader`'s `load_first_book` or a one-shot to print the list; confirm titles/authors/% look right, then remove the temp log.

- [ ] **Step 4: Commit**
```powershell
git add firmware/main/book_loader.hpp firmware/main/book_loader.cpp; git commit -m "feat(firmware): book_loader list_books + load_book + position save/restore"
```

---

## Task 4: BOOT button in `power_bsp`

**Files:** Modify `firmware/components/power_bsp/power_bsp.h`, `firmware/components/power_bsp/power_bsp.c`.

- [ ] **Step 1: Header** — add to `power_bsp.h` (near the shutdown cb):
```cpp
// Fired on a short press of the BOOT button (GPIO0): the menu/back action. Runs in
// the button task; must NOT touch LVGL directly.
void power_bsp_set_boot_cb(power_shutdown_cb_t cb);
```

- [ ] **Step 2: Implementation** — in `power_bsp.c`: add `#define BOOT_BTN_GPIO GPIO_NUM_0`, a `static power_shutdown_cb_t s_boot_cb;`, the setter, configure GPIO0 as input-pullup in `power_bsp_init` (extend the `gpio_config` pin_bit_mask to include both), and detect a BOOT **short press** (press then release within ~1 s) in `power_button_task`:
```cpp
void power_bsp_set_boot_cb(power_shutdown_cb_t cb) { s_boot_cb = cb; }
```
In `power_bsp_init`, change the button `gpio_config` mask to `((uint64_t)1<<PWR_BTN_GPIO) | ((uint64_t)1<<BOOT_BTN_GPIO)`. In `power_button_task`, alongside the PWR long-press logic, add BOOT edge tracking:
```cpp
    bool boot_was_down = false;
    int  boot_held = 0;
    // inside the for(;;) loop, after the PWR handling:
    bool boot_down = (gpio_get_level(BOOT_BTN_GPIO) == 0);
    if (boot_down) { boot_held++; }
    else {
        if (boot_was_down && boot_held < (1000 / kPollMs)) {   // released within ~1s = short press
            ESP_LOGI(TAG, "BOOT short press -> menu/back");
            if (s_boot_cb) s_boot_cb();
        }
        boot_held = 0;
    }
    boot_was_down = boot_down;
```
(Declare `boot_was_down`/`boot_held` outside the loop with the other state.)

- [ ] **Step 3: Build + flash + verify** — [BUILD], [FLASH], [CAPTURE 15]: tap BOOT → `BOOT short press -> menu/back` logged once per tap; PWR long-press still powers off.

- [ ] **Step 4: Commit**
```powershell
git add firmware/components/power_bsp/power_bsp.h firmware/components/power_bsp/power_bsp.c; git commit -m "feat(firmware): BOOT button short-press callback in power_bsp"
```

---

## Task 5: `ui_reader` — open-by-path, settings, resume

**Files:** Modify `firmware/main/ui_reader.h`, `firmware/main/ui_reader.cpp`.

- [ ] **Step 1: Header** — `ui_reader.h`: keep `rsvp_loading_screen_create`/`rsvp_book_ready`/`rsvp_build_reader_screen` for now, add:
```cpp
// Open a specific book by path ("" = sample): show the loading screen, background-load,
// build the reader (resuming from .pos when enabled). Safe to call from the LVGL thread.
void rsvp_open_book_path(const char* path);
// Persist the open book's current word index to its .pos (call on pause / leaving to menu).
void rsvp_reader_save_position(void);
```

- [ ] **Step 2: Apply settings + track path** — in `ui_reader.cpp`: `#include "app_settings.h"`. Add `std::string g_book_path;` and a `font_for(FontSize)` helper:
```cpp
static const lv_font_t* font_for(FontSize f) {
    // Only montserrat_48 is enabled now, so size has no visible effect yet; Task 9
    // enables 36/64 and maps S/M/L -> 36/48/64 here.
    (void)f; return &lv_font_montserrat_48;
}
```
In `build_reader`, set `g_wpm = settings().wpm;`, use `font_for(settings().font)` for `g_pre/g_orp/g_post`, and after creating `g_prev/g_next` set their hidden flag from `settings().show_flankers` (`lv_obj_add/clear_flag(g_prev, LV_OBJ_FLAG_HIDDEN, ...)`). In `set_wpm`, after clamping, add `settings().wpm = g_wpm; settings_save();` so the swipe persists.

- [ ] **Step 3: open-by-path + resume + save** — make `g_index`/`g_book_path` load from a path. Add a path-carrying load task and the two API functions:
```cpp
static std::string g_pending_path;

void load_task(void*) {            // replace the existing body
    auto book = load_book(g_pending_path);
    if (book) { g_index = std::move(book->index); g_loaded_title = book->title; }
    else      { g_index = load_book("")->index;   g_loaded_title = "Sample"; }   // fallback
    g_load_done.store(true);
    vTaskDelete(nullptr);
}

static void open_done_timer_cb(lv_timer_t* t) {
    if (!g_load_done.load()) return;
    if (g_loading_scr) { lv_obj_del(g_loading_scr); g_loading_scr = nullptr; }
    build_reader(g_loaded_title);     // sets g_book_path=g_pending_path; resumes from .pos (Step 2)
    lv_timer_del(t);
}

extern "C" void rsvp_open_book_path(const char* path) {
    g_pending_path = path ? path : "";
    g_load_done.store(false);
    g_loading_scr = lv_obj_create(lv_screen_active());   // full-screen "Loading..." overlay
    lv_obj_remove_style_all(g_loading_scr);
    lv_obj_set_size(g_loading_scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_loading_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_loading_scr, LV_OPA_COVER, 0);
    lv_obj_t* lbl = lv_label_create(g_loading_scr);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xf2f5fa), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
    lv_label_set_text(lbl, "Loading..."); lv_obj_center(lbl);
    xTaskCreatePinnedToCore(load_task, "bookload", 48 * 1024, nullptr, 3, nullptr, 1);
    lv_timer_create(open_done_timer_cb, 50, nullptr);
}
```
(`build_reader` is reused as-is and now runs on the LVGL thread via the timer — see the LVGL-stack bump in Task 6.)
```cpp

extern "C" void rsvp_reader_save_position(void) {
    if (g_player && g_index.wordCount() > 0 && !g_book_path.empty())
        save_position(g_book_path, (uint32_t)g_player->index());
}
```
In `build_reader`, after constructing the `Player`, set `g_book_path = g_pending_path;` and, if `settings().resume_on_open`, `uint32_t p = load_position(g_book_path); if (p < g_index.wordCount()) g_player->seek(p);`. In `tick_cb`, when the player pauses or every ~1500 ticks, call `rsvp_reader_save_position()` — simplest: save in `set_wpm`? No — save on pause: in `touch_event_cb`'s tap (play/pause) when it becomes paused, and add `rsvp_reader_save_position()` there.

- [ ] **Step 4: Build + flash + verify** — [BUILD], [FLASH]. Reader still boots (via `load_first_book`→ now still used by `rsvp_loading_screen_create`). Read a bit, pause, reboot → it resumes near where you paused (check `.pos` is written: serial shows no error; on reboot the first word isn't index 0). Confirm font/flanker settings (defaults) render.

- [ ] **Step 5: Commit**
```powershell
git add firmware/main/ui_reader.h firmware/main/ui_reader.cpp; git commit -m "feat(firmware): reader opens by path, applies settings, resumes via .pos"
```

---

## Task 6: `ui_menu` — menu screen + navigation

**Files:** Create `firmware/main/ui_menu.h`, `firmware/main/ui_menu.cpp`; Modify `firmware/main/main.c`, `firmware/main/ui_reader.h` (expose a "show reader" hook).

- [ ] **Step 1: Header** — `ui_menu.h`:
```cpp
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void ui_menu_init(void);     // register BOOT cb + the nav lv_timer (call once at startup, under LVGL lock)
void ui_menu_open(void);     // show the menu (tiles)
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Nav state machine + menu screen** — `ui_menu.cpp`:
```cpp
#include "ui_menu.h"
#include "ui_reader.h"
#include "lvgl.h"
#include "power_bsp.h"
#include <atomic>

namespace {
enum Screen { SCR_READER, SCR_MENU, SCR_LIBRARY, SCR_SETTINGS, SCR_WIFI };
Screen g_screen = SCR_MENU;
bool   g_book_open = false;          // a book has been opened at least once
std::atomic<bool> g_boot_pressed{false};
lv_obj_t* g_overlay = nullptr;       // current menu/library/settings overlay (nullptr in reader)

void close_overlay() { if (g_overlay) { lv_obj_del(g_overlay); g_overlay = nullptr; } }

lv_obj_t* make_overlay() {           // full-screen black panel over whatever's beneath
    lv_obj_t* o = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(o, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

void show_library();   // Task 7
void show_settings();  // Task 8

void tile_cb(lv_event_t* e) {
    Screen which = (Screen)(intptr_t)lv_event_get_user_data(e);
    close_overlay();
    if (which == SCR_LIBRARY) { g_screen = SCR_LIBRARY; show_library(); }
    else if (which == SCR_SETTINGS) { g_screen = SCR_SETTINGS; show_settings(); }
    else { g_screen = SCR_WIFI; g_overlay = make_overlay();          // WiFi stub
           lv_obj_t* l = lv_label_create(g_overlay); lv_label_set_text(l, "WiFi Drop\n(coming soon)");
           lv_obj_set_style_text_color(l, lv_color_hex(0xf2f5fa), 0); lv_obj_center(l); }
}

void show_menu() {
    g_screen = SCR_MENU;
    g_overlay = make_overlay();
    lv_obj_set_flex_flow(g_overlay, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(g_overlay, 8, 0); lv_obj_set_style_pad_gap(g_overlay, 8, 0);
    const char* names[] = { "Library", "Settings", "WiFi Drop" };
    Screen targets[] = { SCR_LIBRARY, SCR_SETTINGS, SCR_WIFI };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* t = lv_obj_create(g_overlay);
        lv_obj_remove_style_all(t);
        lv_obj_set_flex_grow(t, 1); lv_obj_set_height(t, LV_PCT(100));
        lv_obj_set_style_bg_color(t, lv_color_hex(0x15171c), 0); lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(t, 8, 0);
        lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(t, tile_cb, LV_EVENT_CLICKED, (void*)(intptr_t)targets[i]);
        lv_obj_t* l = lv_label_create(t);
        lv_label_set_text(l, names[i]); lv_obj_set_style_text_color(l, lv_color_hex(0xf2f5fa), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0); lv_obj_center(l);
    }
}

void on_boot() { g_boot_pressed.store(true); }      // from power_bsp button task

void nav_timer_cb(lv_timer_t*) {
    if (!g_boot_pressed.exchange(false)) return;
    switch (g_screen) {
        case SCR_READER:                       close_overlay(); show_menu(); break;
        case SCR_MENU:    if (g_book_open) { close_overlay(); g_screen = SCR_READER; } break;
        case SCR_LIBRARY: case SCR_SETTINGS: case SCR_WIFI:
                          close_overlay(); show_menu(); break;
    }
}
} // namespace

extern "C" void ui_menu_open() { close_overlay(); show_menu(); }

extern "C" void ui_menu_init() {
    power_bsp_set_boot_cb(on_boot);
    lv_timer_create(nav_timer_cb, 80, nullptr);
}
```
Library/Settings (`show_library`/`show_settings`) are stubs that `make_overlay()` + a label until Tasks 7/8.

- [ ] **Step 3: Hook the reader-open to set `g_book_open`** — when the Library opens a book (Task 7), set `g_book_open = true; g_screen = SCR_READER;`. For Task 6, also: in `main.c` keep `rsvp_loading_screen_create()` (boot→reader) but ALSO call `ui_menu_init()` right after (so BOOT opens the menu over the reader). Since the reader is the boot screen here, set `g_screen = SCR_READER; g_book_open = true;` initial values for Task 6 (flip in Task 9). Adjust the two initializers: `Screen g_screen = SCR_READER; bool g_book_open = true;`.

- [ ] **Step 4: main.c** — add `#include "ui_menu.h"`; inside the `if (example_lvgl_lock(-1))` block after `rsvp_loading_screen_create();` add `ui_menu_init();`. Also bump `#define LVGL_TASK_STACK_SIZE` from `(8 * 1024)` to `(16 * 1024)`: the menu/library/settings/reader screens are now built on the LVGL thread (event callbacks + timers), which needs more than the 8 KB default for the object creation + layout pass.

- [ ] **Step 5: Build + flash + verify** — [BUILD], [FLASH]. Reader boots; **tap BOOT → the 3-tile menu appears**; tap BOOT again → back to the reader; tap a tile → its (stub) screen; BOOT → menu. [CAPTURE] confirms BOOT logs.

- [ ] **Step 6: Commit**
```powershell
git add firmware/main/ui_menu.h firmware/main/ui_menu.cpp firmware/main/main.c; git commit -m "feat(firmware): ui_menu tiles + BOOT-driven navigation"
```

---

## Task 7: Library screen

**Files:** Modify `firmware/main/ui_menu.cpp`.

- [ ] **Step 1: Implement `show_library`** — replace the stub; build a scrollable list from `list_books()` (match `library-v2.html`):
```cpp
#include "book_loader.hpp"
// ...
void open_selected(lv_event_t* e) {
    const char* path = (const char*)lv_event_get_user_data(e);
    rsvp_reader_save_position();          // save the (previous) book if one was open
    close_overlay();
    g_book_open = true; g_screen = SCR_READER;
    rsvp_open_book_path(path);            // loading screen -> load -> reader (resumes)
}

void show_library() {
    g_overlay = make_overlay();
    lv_obj_set_flex_flow(g_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_overlay, LV_DIR_VER);
    static std::vector<BookEntry> books;     // static: strings outlive the event cbs
    books = list_books();
    for (auto& b : books) {
        lv_obj_t* row = lv_obj_create(g_overlay);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100)); lv_obj_set_height(row, 52);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x121419), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, open_selected, LV_EVENT_CLICKED, (void*)b.path.c_str());
        lv_obj_t* title = lv_label_create(row);
        lv_label_set_text(title, b.title.c_str());
        lv_obj_set_style_text_color(title, lv_color_hex(0xf2f5fa), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 6);
        lv_obj_t* auth = lv_label_create(row);
        lv_label_set_text(auth, b.author.c_str());
        lv_obj_set_style_text_color(auth, lv_color_hex(0x8893a6), 0);
        lv_obj_align(auth, LV_ALIGN_BOTTOM_LEFT, 16, -6);
        int pct = (b.wordCount > 0) ? (int)((uint64_t)b.position * 100 / b.wordCount) : 0;
        lv_obj_t* pc = lv_label_create(row);
        char buf[8]; snprintf(buf, sizeof buf, "%d%%", pct);
        lv_label_set_text(pc, buf);
        lv_obj_set_style_text_color(pc, lv_color_hex(0xcdd6e6), 0);
        lv_obj_align(pc, LV_ALIGN_RIGHT_MID, -16, 0);
        lv_obj_t* bar = lv_obj_create(row);      // thin red progress bar at the bottom
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, LV_PCT(pct), 3);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xff3b3b), 0); lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
}
```
(`(void*)b.path.c_str()` is valid because `books` is `static` and outlives the screen.)

- [ ] **Step 2: Build + flash + verify** — [BUILD], [FLASH]: BOOT → menu → tap Library → the book list (titles + authors + %); tap a book → loading → reader, resuming if previously read. The "Sample" entry always opens. BOOT returns to the menu.

- [ ] **Step 3: Commit**
```powershell
git add firmware/main/ui_menu.cpp; git commit -m "feat(firmware): Library screen lists + opens SD books with resume"
```

---

## Task 8: Settings screen

**Files:** Modify `firmware/main/ui_menu.cpp`.

- [ ] **Step 1: Implement `show_settings`** — replace the stub; one scrollable list with stepper rows + switch rows (match `settings.html`). Helper to add a row, and apply+save on change:
```cpp
#include "app_settings.h"
#include "lcd_bl_pwm_bsp.h"   // setUpduty for brightness
// Each control updates settings(), calls settings_save(), and applies live.
```
Build rows for: **Reading speed** (`− %d wpm +`, step 25, clamp 100..800; on change `settings().wpm = v; settings_save();`), **Font size** (`− Small/Medium/Large +`; settings_save), **Brightness** (`− %d +` 1..5; `setUpduty(v*51)`; settings_save), **Leading/trailing words** (switch → `settings().show_flankers`; settings_save), **Resume on open** (switch → `settings().resume_on_open`; settings_save). Add disabled **Set clock** and **Est. time** rows (gray "soon"). Use the row markup from Task 7 with a right-aligned control cluster (− / + `lv_button`s ~34px + a value label; or an `lv_switch`). After changing WPM/font here, if a reader exists, it picks up the new value on its next `refresh_word`/`set_wpm`; for font, rebuild applies on next open (acceptable) — note this.

- [ ] **Step 2: Build + flash + verify** — [BUILD], [FLASH]: BOOT → menu → Settings; change speed/brightness → applies live; toggle flankers; **reboot → values persist** (NVS). Brightness changes the backlight immediately.

- [ ] **Step 3: Commit**
```powershell
git add firmware/main/ui_menu.cpp; git commit -m "feat(firmware): Settings screen (speed/font/brightness/flankers/resume), persisted"
```

---

## Task 9: Boot→menu, touch fix, font sizes

**Files:** Modify `firmware/main/main.c`, `firmware/main/ui_menu.cpp`, `firmware/sdkconfig.defaults`.

- [ ] **Step 1: Boot→menu** — in `main.c`, replace `rsvp_loading_screen_create();` with `ui_menu_init(); ui_menu_open();` (the menu is now the boot screen; books load on demand). In `ui_menu.cpp` change the initializers to `Screen g_screen = SCR_MENU; bool g_book_open = false;`. (`rsvp_loading_screen_create` is now unused → it can stay or be removed; `rsvp_open_book_path` is the entry.)

- [ ] **Step 2: Touch de-mirror** — in `main.c`, set `.mirror_y = 1` in the `esp_lcd_touch_config_t` flags. Rebuild; on-device, verify menu/list/settings **taps land on the right item**.

- [ ] **Step 3: Swipe re-check** — with `mirror_y=1`, re-test the reader's left/right swipe (sentence prev/next). If reversed, swap the two branches in `ui_reader`'s gesture handler so swipe-right = next sentence again.

- [ ] **Step 4: Font sizes** — enable `CONFIG_LV_FONT_MONTSERRAT_36=y` and `CONFIG_LV_FONT_MONTSERRAT_64=y` in `firmware/sdkconfig.defaults`, then update `font_for` in `ui_reader.cpp` to map `FONT_SMALL/FONT_MEDIUM/FONT_LARGE` → `&lv_font_montserrat_36 / _48 / _64`. Rebuild.

- [ ] **Step 5: Build + flash + verify** — [BUILD], [FLASH]: **boot → Menu**; Library → open book → reader; BOOT toggles Reader↔Menu; Settings persist; taps land correctly; font-size Small/Medium/Large visibly differ; power button still works.

- [ ] **Step 6: Commit + notes**
```powershell
git add firmware/main/main.c firmware/main/ui_menu.cpp firmware/sdkconfig.defaults; git commit -m "feat(firmware): boot to menu + touch mirror_y=1 + font sizes"
```
Then update `docs/firmware-notes.md`: a "Menu / Library / Settings (Phase 2A)" bullet (BOOT-button menu, Library with resume via `.pos`, Settings in NVS, `mirror_y=1`); commit `docs: Phase 2A menu/library/settings notes`.
```

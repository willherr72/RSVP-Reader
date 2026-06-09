# SD Card Mount + EPUB/TXT Loading Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On boot, mount the SD card, load the first EPUB/TXT on it (compile-and-cache to a `/.rsvp/` sidecar), and read it in the RSVP reader — falling back to the built-in sample on any failure.

**Architecture:** A thin firmware loader over the already host-tested `core` document pipeline. New: a `miniz` component + registering `core`'s zip/parse/index sources for the firmware build; a `sdcard_bsp` SDMMC mount component; a `book_loader` module (scan → load-or-compile → `Document`); one new host-tested pure function `cacheIndexPath`. The C-linkage `rsvp_reading_screen_create()` does the C++ loading internally, so `main.c` only adds `sdcard_init()`.

**Tech Stack:** ESP-IDF 5.5.2 (SDMMC + FATFS VFS), LVGL 9, C++17 `core` + vendored miniz, doctest host tests.

---

## Prerequisite (arrange before Task 3)

On-device verification needs a **FAT32 microSD card with a real test book** at the top level — at minimum one `.epub`, ideally also a `.txt`. The user inserts it into the board's TF slot. (Have a known title/author in the EPUB so the displayed metadata can be checked.)

## Conventions (referenced by tasks)

**[BUILD]** / **[FLASH]** — same as before:
```powershell
$idf = "C:\Users\WilliamHerr\esp\v5.5.2\esp-idf"; $env:IDF_PATH = $idf; $env:IDF_PYTHON_ENV_PATH = "C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env"; & "$idf\export.ps1" *> $null; idf.py -C "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\firmware" build
```
Flash: same env prefix, then `idf.py -C "...\firmware" -p COM32 flash`

**[HOST-TEST]**:
```powershell
cmake --build "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\build\host" --config Debug
& "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\build\host\Debug\rsvp_tests.exe"
```

**[CAPTURE n]** — capture n seconds of serial (boot/loader logs). Create once:
`firmware/build/cap.py`:
```python
import serial, sys, time
dur = int(sys.argv[1]) if len(sys.argv) > 1 else 12
s = serial.Serial(); s.port = "COM32"; s.baudrate = 115200
s.dtr = False; s.rts = False; s.timeout = 0.2; s.open()
end = time.time() + dur
while time.time() < end:
    line = s.readline()
    if line: sys.stdout.write(line.decode("utf-8","replace")); sys.stdout.flush()
s.close(); sys.stdout.write("\n=== capture complete ===\n")
```
Run (start it, then it catches the post-flash boot logs):
```powershell
& "C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe" "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\firmware\build\cap.py" 12
```

---

## Task 1: `cacheIndexPath` pure function (host-tested)

**Files:**
- Create: `core/include/rsvp/cachepath.hpp`, `core/src/cachepath.cpp`
- Test: `test/host/test_cachepath.cpp`
- Modify: `test/host/CMakeLists.txt`, `core/CMakeLists.txt`

- [ ] **Step 1: Header + stub** — `core/include/rsvp/cachepath.hpp`:
```cpp
#pragma once
#include <string>

namespace rsvp {

// Map a book path to its compiled-index cache path in a sibling ".rsvp" dir,
// keeping the full filename (incl. extension) so Foo.epub and Foo.txt don't
// collide. e.g. "/sdcard/Foo.epub" -> "/sdcard/.rsvp/Foo.epub.idx". Deterministic.
std::string cacheIndexPath(const std::string& bookPath);

} // namespace rsvp
```
`core/src/cachepath.cpp` (stub so the test goes red on behavior, not a link error):
```cpp
#include "rsvp/cachepath.hpp"

namespace rsvp {
std::string cacheIndexPath(const std::string&) { return ""; }
} // namespace rsvp
```

- [ ] **Step 2: Failing test** — `test/host/test_cachepath.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/cachepath.hpp"
using namespace rsvp;

TEST_CASE("cacheIndexPath puts the .idx in a sibling .rsvp dir") {
    CHECK(cacheIndexPath("/sdcard/Foo.epub") == "/sdcard/.rsvp/Foo.epub.idx");
}
TEST_CASE("cacheIndexPath keeps the extension so .epub and .txt don't collide") {
    CHECK(cacheIndexPath("/sdcard/Foo.epub") == "/sdcard/.rsvp/Foo.epub.idx");
    CHECK(cacheIndexPath("/sdcard/Foo.txt")  == "/sdcard/.rsvp/Foo.txt.idx");
}
TEST_CASE("cacheIndexPath preserves spaces and nests beside the file") {
    CHECK(cacheIndexPath("/sdcard/My Book.epub") == "/sdcard/.rsvp/My Book.epub.idx");
    CHECK(cacheIndexPath("/sdcard/sub/Bar.epub") == "/sdcard/sub/.rsvp/Bar.epub.idx");
}
```

- [ ] **Step 3: Register** — in `test/host/CMakeLists.txt` add `test_cachepath.cpp` to the `add_executable` list (after `test_gesture.cpp`) and `${CORE_DIR}/src/cachepath.cpp` (after `${CORE_DIR}/src/gesture.cpp`). In `core/CMakeLists.txt` add `"src/cachepath.cpp"` to `SRCS` (after `"src/gesture.cpp"`).

- [ ] **Step 4: Run, expect red** — **[HOST-TEST]**. Expected: build OK, the `cacheIndexPath` cases FAIL (stub returns "").

- [ ] **Step 5: Implement** — overwrite `core/src/cachepath.cpp`:
```cpp
#include "rsvp/cachepath.hpp"

namespace rsvp {

std::string cacheIndexPath(const std::string& bookPath) {
    const auto slash = bookPath.find_last_of('/');
    const std::string dir  = (slash == std::string::npos) ? std::string() : bookPath.substr(0, slash);
    const std::string name = (slash == std::string::npos) ? bookPath : bookPath.substr(slash + 1);
    return dir + "/.rsvp/" + name + ".idx";
}

} // namespace rsvp
```

- [ ] **Step 6: Run, expect green** — **[HOST-TEST]**. Expected: all PASS (prior total + 3 new cases).

- [ ] **Step 7: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add core/include/rsvp/cachepath.hpp core/src/cachepath.cpp test/host/test_cachepath.cpp test/host/CMakeLists.txt core/CMakeLists.txt; git commit -m "feat(core): host-tested cacheIndexPath for the .rsvp sidecar"
```

---

## Task 2: Build the full `core` pipeline (+ miniz) for firmware

**Files:**
- Create: `firmware/components/miniz/CMakeLists.txt`
- Modify: `core/CMakeLists.txt`

- [ ] **Step 1: miniz component** — `firmware/components/miniz/CMakeLists.txt`:
```cmake
# Wrap the vendored miniz (repo/third_party/miniz) as an ESP-IDF component.
idf_component_register(
    SRCS "../../../third_party/miniz/miniz.c"
    INCLUDE_DIRS "../../../third_party/miniz")
```

- [ ] **Step 2: Register the rest of core + require miniz** — replace the body of `core/CMakeLists.txt`'s `idf_component_register` with:
```cmake
idf_component_register(
    SRCS
        "src/orp.cpp"
        "src/gesture.cpp"
        "src/cachepath.cpp"
        "src/pacing.cpp"
        "src/player.cpp"
        "src/tokenize.cpp"
        "src/entity.cpp"
        "src/htmltext.cpp"
        "src/opf.cpp"
        "src/index.cpp"
        "src/zipreader.cpp"
        "src/epub.cpp"
    INCLUDE_DIRS "include"
    REQUIRES miniz)
```

- [ ] **Step 3: Build** — **[BUILD]**. Expected: `Project build complete` — the whole document pipeline now compiles on-device. (No behavior change yet; this de-risks the heavy core compile + miniz wiring before adding hardware code.) If miniz's out-of-component `SRCS` path errors, fall back to folding it into `core` (`SRCS … "../third_party/miniz/miniz.c"`, `INCLUDE_DIRS "include" "../third_party/miniz"`, drop `REQUIRES miniz`).

- [ ] **Step 4: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add core/CMakeLists.txt firmware/components/miniz/CMakeLists.txt; git commit -m "build(firmware): compile full core pipeline + miniz on-device"
```

---

## Task 3: `sdcard_bsp` component + mount on boot

**Files:**
- Create: `firmware/components/sdcard_bsp/CMakeLists.txt`, `sdcard_bsp.h`, `sdcard_bsp.c`
- Modify: `firmware/main/main.c`

- [ ] **Step 1: Component CMake** — `firmware/components/sdcard_bsp/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "sdcard_bsp.c"
    INCLUDE_DIRS "."
    REQUIRES fatfs sdmmc esp_driver_sdmmc)
```

- [ ] **Step 2: Header** — `firmware/components/sdcard_bsp/sdcard_bsp.h`:
```c
#ifndef SDCARD_BSP_H
#define SDCARD_BSP_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

// Mount the SDMMC TF card (1-line) at "/sdcard". Safe to call once at boot;
// logs and leaves g_sd_mounted=false on failure (no abort).
void sdcard_init(void);
bool sdcard_mounted(void);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 3: Implementation** — `firmware/components/sdcard_bsp/sdcard_bsp.c` (SDMMC 1-line, pins from the Waveshare example):
```c
#include "sdcard_bsp.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

#define SDMMC_CLK_PIN   GPIO_NUM_41
#define SDMMC_CMD_PIN   GPIO_NUM_39
#define SDMMC_D0_PIN    GPIO_NUM_40
#define SD_MOUNT_POINT  "/sdcard"

static const char *TAG = "sdcard_bsp";
static sdmmc_card_t *s_card = NULL;

void sdcard_init(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = SDMMC_CLK_PIN;
    slot_config.cmd = SDMMC_CMD_PIN;
    slot_config.d0  = SDMMC_D0_PIN;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config,
                                            &mount_config, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
        return;
    }
    ESP_LOGI(TAG, "SD mounted at %s", SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_card);
}

bool sdcard_mounted(void) { return s_card != NULL; }
```

- [ ] **Step 4: Mount at boot** — in `firmware/main/main.c`, add `#include "sdcard_bsp.h"` with the other component includes, and in `app_main` immediately after `touch_i2c_master_Init();` add:
```c
    sdcard_init();
```

- [ ] **Step 5: Build, flash, verify mount** — **[BUILD]**, **[FLASH]**, then **[CAPTURE 12]** (insert the SD card first). Expected serial: `sdcard_bsp: SD mounted at /sdcard` followed by `sdmmc_card_print_info` output (card name/size). If it logs `SD mount failed`, stop and diagnose (card seated? FAT32?).

- [ ] **Step 6: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add firmware/components/sdcard_bsp firmware/main/main.c; git commit -m "feat(firmware): mount SDMMC TF card at /sdcard on boot"
```

---

## Task 4: `book_loader` module (scan → load-or-compile) + serial-log verify

**Files:**
- Create: `firmware/main/book_loader.hpp`, `firmware/main/book_loader.cpp`
- Modify: `firmware/main/CMakeLists.txt`, `firmware/main/main.c` (temporary verify call)

- [ ] **Step 1: Header** — `firmware/main/book_loader.hpp`:
```cpp
#pragma once
#include "rsvp/token.hpp"
#include <optional>
#include <string>

struct LoadedBook {
    rsvp::Document doc;
    std::string    title;
    std::string    author;
};

// Scan /sdcard for the first .epub/.txt, load-or-compile its index (cached in
// /sdcard/.rsvp/), and return it. nullopt on no-card/no-book/parse failure.
std::optional<LoadedBook> load_first_book();
```

- [ ] **Step 2: Implementation** — `firmware/main/book_loader.cpp`:
```cpp
#include "book_loader.hpp"
#include "sdcard_bsp.h"
#include "rsvp/cachepath.hpp"
#include "rsvp/epub.hpp"
#include "rsvp/index.hpp"
#include "rsvp/tokenize.hpp"

#include "esp_log.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

static const char *TAG = "book_loader";
// Source-file cap. Large std::vector allocations (>16KB) land in PSRAM automatically
// (CONFIG_SPIRAM_USE_MALLOC=y, ALWAYSINTERNAL=16384); 4MB leaves PSRAM headroom for the
// decompressed text, the compiled index, and LVGL's framebuffers.
static constexpr std::size_t kMaxBookBytes = 4u * 1024 * 1024;

namespace {

bool ends_with_ci(const std::string& s, const char* suffix) {
    const std::size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i)
        if (std::tolower((unsigned char)s[s.size() - n + i]) != suffix[i]) return false;
    return true;
}

// First top-level .epub/.txt in /sdcard (skips ".rsvp" and dotfiles). "" if none.
std::string find_first_book() {
    DIR* d = opendir("/sdcard");
    if (!d) return "";
    std::string best;
    for (dirent* e = readdir(d); e; e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string name = e->d_name;
        if (ends_with_ci(name, ".epub") || ends_with_ci(name, ".txt")) {
            best = "/sdcard/" + name;
            break;
        }
    }
    closedir(d);
    return best;
}

bool read_file(const std::string& path, std::vector<std::uint8_t>& out) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || (std::size_t)st.st_size > kMaxBookBytes) return false;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.resize(st.st_size);
    const std::size_t got = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size();
}

std::string filename_stem(const std::string& path) {
    std::size_t slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    std::size_t dot = name.find_last_of('.');
    return (dot == std::string::npos) ? name : name.substr(0, dot);
}

} // namespace

std::optional<LoadedBook> load_first_book() {
    if (!sdcard_mounted()) { ESP_LOGW(TAG, "no SD card"); return std::nullopt; }

    const std::string book = find_first_book();
    if (book.empty()) { ESP_LOGW(TAG, "no .epub/.txt on card"); return std::nullopt; }
    ESP_LOGI(TAG, "book: %s", book.c_str());

    struct stat st;
    if (stat(book.c_str(), &st) != 0) return std::nullopt;
    const std::uint32_t size  = (std::uint32_t)st.st_size;
    const std::uint32_t mtime = (std::uint32_t)st.st_mtime;
    const std::string idxPath = rsvp::cacheIndexPath(book);

    std::vector<std::uint8_t> idx;
    std::vector<std::uint8_t> cached;
    if (read_file(idxPath, cached) && rsvp::indexMatchesSource(cached, size, mtime)) {
        ESP_LOGI(TAG, "cache hit: %s", idxPath.c_str());
        idx = std::move(cached);
    } else {
        std::vector<std::uint8_t> raw;
        if (!read_file(book, raw)) { ESP_LOGW(TAG, "read failed"); return std::nullopt; }
        if (ends_with_ci(book, ".epub")) {
            idx = rsvp::epubToIndex(raw, size, mtime);
        } else {
            rsvp::DocMeta meta;
            meta.title = filename_stem(book);
            meta.sourceSize = size; meta.sourceMtime = mtime;
            std::string text((const char*)raw.data(), raw.size());
            idx = rsvp::serializeIndex(rsvp::tokenizePlainText(text), meta, {});
        }
        if (idx.empty()) { ESP_LOGW(TAG, "compile failed"); return std::nullopt; }
        mkdir("/sdcard/.rsvp", 0777);
        FILE* f = fopen(idxPath.c_str(), "wb");
        if (f) { fwrite(idx.data(), 1, idx.size(), f); fclose(f); ESP_LOGI(TAG, "cached %s", idxPath.c_str()); }
        else   { ESP_LOGW(TAG, "cache write failed (continuing)"); }
    }

    rsvp::CompiledIndex ci = rsvp::CompiledIndex::parse(idx);
    if (!ci.ok()) { ESP_LOGW(TAG, "index parse failed"); return std::nullopt; }
    LoadedBook lb{ ci.toDocument(), ci.meta().title, ci.meta().author };
    ESP_LOGI(TAG, "loaded \"%s\" by %s, %u words",
             lb.title.c_str(), lb.author.c_str(), (unsigned)lb.doc.size());
    return lb;
}
```

- [ ] **Step 3: Register in main** — in `firmware/main/CMakeLists.txt` change the SRCS line to:
```cmake
    SRCS "main.c" "ui_reader.cpp" "book_loader.cpp"
```

- [ ] **Step 4: Temporary verify call** — in `firmware/main/main.c`, after `sdcard_init();` add a temporary block (removed in Task 5). At the top of `main.c` add `#include "book_loader.hpp"` — but `main.c` is C; instead call a tiny C shim. Simplest: in `app_main`, right after `sdcard_init();`, add:
```c
    extern void book_loader_logtest(void);   // defined in book_loader.cpp
    book_loader_logtest();
```
and add to the bottom of `firmware/main/book_loader.cpp`:
```cpp
extern "C" void book_loader_logtest(void) { (void)load_first_book(); }
```

- [ ] **Step 5: Build, flash, verify** — **[BUILD]**, **[FLASH]**, **[CAPTURE 15]** (card with a known `.epub` inserted). Expected serial: `book_loader: book: /sdcard/<name>.epub`, then `compile failed`→no (should compile), `cached /sdcard/.rsvp/<name>.epub.idx`, then `loaded "<Title>" by <Author>, <N> words`. Reboot (re-flash or power-cycle) + **[CAPTURE 15]** again → `cache hit:` line instead of `cached`.

- [ ] **Step 6: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add firmware/main/book_loader.hpp firmware/main/book_loader.cpp firmware/main/CMakeLists.txt firmware/main/main.c; git commit -m "feat(firmware): book_loader scans SD + compile-and-caches an index"
```

---

## Task 5: Read the loaded book in the reader (+ remove temp call)

**Files:**
- Modify: `firmware/main/ui_reader.cpp`, `firmware/main/main.c`

- [ ] **Step 1: Remove the temp verify call** — in `firmware/main/main.c` delete the `book_loader_logtest` extern + call added in Task 4 Step 4. In `firmware/main/book_loader.cpp` delete the `book_loader_logtest` shim at the bottom.

- [ ] **Step 2: Wire the loaded book in** — in `firmware/main/ui_reader.cpp` add `#include "book_loader.hpp"` (with the other rsvp includes). The title label is created near the TOP of `rsvp_reading_screen_create`, so load the book up there. **(a)** Right after the background setup:
```cpp
    g_scr = lv_screen_active();
    lv_obj_set_style_bg_color(g_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);
```
insert:
```cpp
    // Load the first SD book up front (its title goes in the status bar); fall
    // back to the built-in sample if there's no card/book.
    std::string book_title;
    if (auto book = load_first_book()) {
        g_doc      = std::move(book->doc);
        book_title = book->title;
    } else {
        g_doc = tokenizePlainText(
            "Rapid serial visual presentation shows one word at a time. "
            "Your eyes stay still while the words flow past you. "
            "This little reader is now alive on the hardware!");
        book_title = "Sample";
    }
```
**(b)** `g_doc` is loaded now, so remove the original sample assignment lower in the function:
```cpp
    // --- engine: tokenize a built-in sample and drive the Player ---
    g_doc = tokenizePlainText(
        "Rapid serial visual presentation shows one word at a time. "
        "Your eyes stay still while the words flow past you. "
        "This little reader is now alive on the hardware!");
    PacingConfig cfg;
```
to:
```cpp
    // --- engine: drive the Player over the loaded document ---
    PacingConfig cfg;
```

- [ ] **Step 3: Show the title** — the top-left status label is created after the load block (so `book_title` is in scope). Change it from `"84%"`:
```cpp
    lv_obj_t *batt = make_label(g_scr, dim, &lv_font_montserrat_16);
    lv_label_set_text(batt, "84%");
    lv_obj_align(batt, LV_ALIGN_TOP_LEFT, 10, 6);
```
to:
```cpp
    lv_obj_t *batt = make_label(g_scr, dim, &lv_font_montserrat_16);
    lv_label_set_long_mode(batt, LV_LABEL_LONG_DOT);
    lv_obj_set_width(batt, 360);
    lv_label_set_text(batt, book_title.c_str());
    lv_obj_align(batt, LV_ALIGN_TOP_LEFT, 10, 6);
```

- [ ] **Step 4: Build, flash, end-to-end verify** — **[BUILD]**, **[FLASH]** with the EPUB card inserted. On-device:
  - The reader streams the **real book's** words (not the sample), title shown top-left.
  - Tap/swipe gestures still work (play/pause, WPM, sentence).
  - **[CAPTURE 12]** shows `cache hit` (the `.idx` from Task 4 still matches).
  - Remove the card / use a blank card → re-flash → reads the **sample**, title shows "Sample".

- [ ] **Step 5: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add firmware/main/ui_reader.cpp firmware/main/main.c firmware/main/book_loader.cpp; git commit -m "feat(firmware): read the first SD book in the reader, sample fallback"
```

- [ ] **Step 6: Update handoff notes** — in `docs/firmware-notes.md` add an SD/EPUB status line (mounts SDMMC /sdcard, loads first EPUB/TXT, compile-and-cache to /.rsvp/, sample fallback; resume + library are Phase 2). Commit:
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add docs/firmware-notes.md; git commit -m "docs: SD card + EPUB/TXT loading working"
```

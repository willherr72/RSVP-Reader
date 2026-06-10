# ETA to Finish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show estimated time-to-finish in the reader's bottom-right, hideable in Settings.

**Architecture:** A pure `(remainingWords, wpm) → "2h 15m"` formatter in host-tested `core/eta`. The reader's existing `update_status()` (already called on word-advance, speed change, and settings-apply) writes a bottom-right label, gated by a `show_eta` setting.

**Tech Stack:** LVGL 9, C++17 `core` (doctest).

---

## Conventions

**[HOST]:** `cmake --build "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\build\host" --config Debug` then `& "...\build\host\Debug\rsvp_tests.exe"`.
**[BUILD]/[FLASH]:** `$idfp="C:\Users\WilliamHerr\esp\v5.5.2\esp-idf"; $env:IDF_PATH=$idfp; $env:IDF_PYTHON_ENV_PATH="C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env"; & "$idfp\export.ps1" *> $null; idf.py -C "...\firmware" build` (flash: `-p COM32 flash`).

---

## Task 1: `core/eta` — ETA string formatter (host-TDD)

**Files:** Create `core/include/rsvp/eta.hpp`, `core/src/eta.cpp`, `test/host/test_eta.cpp`; Modify `core/CMakeLists.txt`, `test/host/CMakeLists.txt`.

- [ ] **Step 1: Header** — `core/include/rsvp/eta.hpp`:
```cpp
#pragma once
#include <string>
namespace rsvp {

std::string etaString(int remainingWords, int wpm);   // "2h 15m" / "45m" / "<1m" / "" (done/invalid)

} // namespace rsvp
```

- [ ] **Step 2: Failing test** — `test/host/test_eta.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/eta.hpp"
using namespace rsvp;

TEST_CASE("etaString formats hours and minutes") {
    CHECK(etaString(40500, 300) == "2h 15m");
    CHECK(etaString(36000, 300) == "2h");      // exact hour -> no minutes
    CHECK(etaString(18000, 300) == "1h");
    CHECK(etaString(18300, 300) == "1h 1m");
    CHECK(etaString(13500, 300) == "45m");
}

TEST_CASE("etaString near-zero and invalid") {
    CHECK(etaString(100, 300) == "<1m");       // rounds to 0 minutes
    CHECK(etaString(0, 300) == "");            // nothing left
    CHECK(etaString(-5, 300) == "");
    CHECK(etaString(9000, 0) == "");           // no speed
}
```

- [ ] **Step 3: Register sources** — in `core/CMakeLists.txt` add `"src/eta.cpp"` to `SRCS`; in `test/host/CMakeLists.txt` add `test_eta.cpp` (test list) and `${CORE_DIR}/src/eta.cpp` (core-source list).

- [ ] **Step 4: Run, expect red** — [HOST]: link error (`etaString` undefined).

- [ ] **Step 5: Implement** — `core/src/eta.cpp`:
```cpp
#include "rsvp/eta.hpp"
namespace rsvp {

std::string etaString(int remainingWords, int wpm) {
    if (wpm <= 0 || remainingWords <= 0) return "";
    int mins = (remainingWords + wpm / 2) / wpm;          // round to nearest minute
    if (mins < 1)  return "<1m";
    if (mins < 60) return std::to_string(mins) + "m";
    int h = mins / 60, m = mins % 60;
    if (m == 0)    return std::to_string(h) + "h";
    return std::to_string(h) + "h " + std::to_string(m) + "m";
}

} // namespace rsvp
```

- [ ] **Step 6: Run, expect green** — [HOST]: all pass.

- [ ] **Step 7: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add core/include/rsvp/eta.hpp core/src/eta.cpp test/host/test_eta.cpp core/CMakeLists.txt test/host/CMakeLists.txt; git commit -m "feat(core): etaString time-to-finish formatter"
```

---

## Task 2: ETA in the reader + Settings toggle

**Files:** Modify `firmware/main/app_settings.h`, `firmware/main/app_settings.cpp`, `firmware/main/ui_reader.cpp`, `firmware/main/ui_menu.cpp`.

- [ ] **Step 1: Setting** — in `app_settings.h` add to `Settings`: `bool show_eta = true;`. In `app_settings.cpp` `settings_load`, after the `rot` line add `if (nvs_get_u8(h, "eta", &b) == ESP_OK) s_settings.show_eta = b;`; in `settings_save`, after the `rot` line add `nvs_set_u8 (h, "eta", s_settings.show_eta);`.

- [ ] **Step 2: Reader label + status update** — in `ui_reader.cpp` add `#include "rsvp/eta.hpp"` (near the other `rsvp/` includes). Next to `lv_obj_t *g_wpm_lbl = nullptr;` add:
```cpp
lv_obj_t *g_eta = nullptr;     // bottom-right time-to-finish label
```
In `update_status()`, after `lv_label_set_text(g_wpm_lbl, buf);` add:
```cpp
    if (g_eta) {
        if (settings().show_eta) {
            int rem = (int)g_index.wordCount() - (int)g_player->index();
            lv_label_set_text(g_eta, etaString(rem, g_wpm).c_str());
        } else {
            lv_label_set_text(g_eta, "");
        }
    }
```

- [ ] **Step 3: Create the label** — in `build_reader`, after the bottom `g_wpm_lbl` block:
```cpp
    g_wpm_lbl = make_label(g_scr, dim, &lv_font_montserrat_16);
    lv_obj_align(g_wpm_lbl, LV_ALIGN_BOTTOM_MID, 0, -6);
```
add:
```cpp
    g_eta = make_label(g_scr, dim, &lv_font_montserrat_16);
    lv_obj_align(g_eta, LV_ALIGN_BOTTOM_RIGHT, -10, -6);
```

- [ ] **Step 4: Settings switch** — in `ui_menu.cpp` `show_settings()`, after the `add_switch("Auto-rotate", 3, settings().auto_rotate);` line add:
```cpp
    add_switch("Show ETA", 4, settings().show_eta);
```
and in `switch_cb`, change the final `else` (currently auto_rotate) so id 3 stays auto_rotate and the new id 4 handles `show_eta` with a live refresh:
```cpp
    else if (which == 3) { settings().auto_rotate = on; settings_save(); }
    else                 { settings().show_eta = on; settings_save(); rsvp_reader_apply_settings(); }
```

- [ ] **Step 5: Build + flash + verify** — **[BUILD]**, **[FLASH]**. Open a book → the **bottom-right** shows a time like `2h 15m`, decreasing as you read (and the bottom-mid `wpm · %` is unchanged). **Swipe up/down** to change WPM → the ETA jumps (faster = less time). Near the end it reads `<1m` then blanks at 100%. **Settings → Show ETA off** → it disappears immediately on returning to the reader; reboot → still off.

- [ ] **Step 6: Commit + notes**
```powershell
git add firmware/main/app_settings.h firmware/main/app_settings.cpp firmware/main/ui_reader.cpp firmware/main/ui_menu.cpp; git commit -m "feat(firmware): ETA-to-finish in the reader + Show ETA toggle"
```
Then add an ETA bullet to `docs/firmware-notes.md` (bottom-right time-to-finish from words/WPM, `core/eta`, Show ETA setting) and commit `docs: ETA notes`.

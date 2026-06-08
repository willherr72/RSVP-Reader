# RSVP Engine Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the portable, host-tested C++ core of the RSVP reader — token model, ORP pivot, pacing/timing, and the playback state machine — with a passing unit-test suite, no hardware involved.

**Architecture:** A platform-independent C++17 library under `core/` (no ESP-IDF, no LVGL, no Arduino). It is exercised by a host CMake build under `test/host/` using the single-header [doctest] framework. The same `core/` sources later compile unchanged into the ESP-IDF firmware as a component. Strict TDD: red → green → commit.

**Tech Stack:** C++17, CMake (≥3.16) with the Visual Studio 2022 (MSVC) generator, doctest v2.4.11 (vendored single header).

---

## Scope & rationale

This plan is **only** the host-testable engine core — the logic heart of the product and the root of the dependency graph. It deliberately excludes hardware bring-up, the document/file pipeline, and any UI. Per the design spec (§4, §12), the engine is pure logic with no hardware dependencies and is unit-tested on a host; building it first lets us prove the reading behavior with fast, deterministic tests before touching the panel.

**Out of scope here (future plans):** TXT tokenizer + compiled-index codec (Plan 2); ESP-IDF project + HAL + LVGL bring-up (Plan 3); library/settings UI (Plan 4); Wi-Fi upload (Plan 5); EPUB indexer (Plan 6); PDF indexer (Plan 7).

**Definition of done:** `cmake --build build/host && ./build/host/Debug/rsvp_tests.exe` prints `[doctest] Status: SUCCESS!` with every test passing, covering ORP, pacing, and the player state machine.

## File structure

```
core/include/rsvp/token.hpp    — Token + Document model            (header-only)
core/include/rsvp/utf8.hpp     — UTF-8 length / byte-offset helpers (header-only)
core/include/rsvp/orp.hpp      — ORP pivot API
core/src/orp.cpp               — ORP implementation
core/include/rsvp/pacing.hpp   — pacing config + duration API
core/src/pacing.cpp            — pacing implementation
core/include/rsvp/player.hpp   — playback state-machine API
core/src/player.cpp            — playback implementation
test/host/CMakeLists.txt       — host build (compiles core sources + tests)
test/host/doctest.h            — vendored test framework (pinned v2.4.11)
test/host/test_main.cpp        — doctest entry point
test/host/test_token.cpp       — Token model smoke test
test/host/test_orp.cpp         — ORP tests
test/host/test_pacing.cpp      — pacing tests
test/host/test_player.cpp      — player tests
```

Each unit is a small file with one responsibility (model / utf8 / orp / pacing / player). `token.hpp` and `utf8.hpp` are header-only; the rest split declaration (`.hpp`) from implementation (`.cpp`).

## Conventions

- **TDD:** write the failing test, watch it fail, implement minimally, watch it pass, commit.
- **Toolchain:** CMake drives the installed **Visual Studio 2022 Build Tools (MSVC)** via the `-G "Visual Studio 17 2022"` generator — no Ninja or compiler-on-PATH needed. The VS generator is multi-config and defaults to **Debug**, so `cmake --build build/host` emits `build/host/Debug/rsvp_tests.exe`.
- **Build dir** is `build/host/` (git-ignored — Step 0.1 adds it to `.gitignore`).
- **Running tests:** `./build/host/Debug/rsvp_tests.exe` from the repo root via the msys/Git-Bash shell.
- **Commit messages** end with the trailer:
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`
- **Branch:** work happens on `feature/rsvp-engine-core` (already created off the docs branch that carries the spec+plan, itself branched from `main`). Do not implement on `main`.

---

### Task 0: Host build skeleton + Token model

**Files:**
- Modify: `.gitignore`
- Create: `test/host/doctest.h` (downloaded)
- Create: `test/host/test_main.cpp`
- Create: `core/include/rsvp/token.hpp`
- Create: `test/host/test_token.cpp`
- Create: `test/host/CMakeLists.txt`

- [ ] **Step 0.1: Verify toolchain and ignore the build dir**

Run:
```bash
cmake --version   # expect >= 3.16
```
Expected: CMake prints a version. The build uses the `-G "Visual Studio 17 2022"` generator (MSVC from the installed VS 2022 Build Tools), so no Ninja or compiler-on-PATH is required — CMake locates the toolset itself.

Append to `.gitignore`:
```
# Host build output
/build/
```

- [ ] **Step 0.2: Vendor the doctest header (pinned)**

Run:
```bash
mkdir -p test/host core/include/rsvp core/src
curl -L -o test/host/doctest.h \
  https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
```
Expected: `test/host/doctest.h` exists and is ~7,500+ lines.

- [ ] **Step 0.3: Create the doctest entry point**

Create `test/host/test_main.cpp`:
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
```

- [ ] **Step 0.4: Create the Token / Document model**

Create `core/include/rsvp/token.hpp`:
```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rsvp {

// Structural flags attached to a token; consumed by the pacing engine.
enum TokenFlags : std::uint8_t {
    FLAG_NONE          = 0,
    FLAG_SENTENCE_END  = 1u << 0,  // token ends a sentence (. ! ?)
    FLAG_PARAGRAPH_END = 1u << 1,  // token ends a paragraph
    FLAG_CHAPTER_START = 1u << 2,  // token starts a chapter
};

struct Token {
    std::string  text;            // the word as displayed (UTF-8)
    std::uint8_t flags = FLAG_NONE;
    bool has(TokenFlags f) const {
        return (flags & static_cast<std::uint8_t>(f)) != 0;
    }
};

// In-memory document: an ordered list of tokens.
struct Document {
    std::vector<Token> tokens;
    std::size_t size()  const { return tokens.size(); }
    bool        empty() const { return tokens.empty(); }
};

} // namespace rsvp
```

- [ ] **Step 0.5: Write the smoke test**

Create `test/host/test_token.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/token.hpp"
using namespace rsvp;

TEST_CASE("Token flags and Document basics") {
    Token t{"Hello.", FLAG_SENTENCE_END};
    CHECK(t.has(FLAG_SENTENCE_END));
    CHECK_FALSE(t.has(FLAG_PARAGRAPH_END));

    Document d;
    CHECK(d.empty());
    d.tokens.push_back(t);
    CHECK(d.size() == 1);
    CHECK_FALSE(d.empty());
}
```

- [ ] **Step 0.6: Create the host CMake build**

Create `test/host/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
project(rsvp_core_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../core)

add_executable(rsvp_tests
    test_main.cpp
    test_token.cpp
)
target_include_directories(rsvp_tests PRIVATE
    ${CORE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

enable_testing()
add_test(NAME rsvp_tests COMMAND rsvp_tests)
```

- [ ] **Step 0.7: Configure, build, run → PASS**

Run (from the repo root):
```bash
cmake -S test/host -B build/host -G "Visual Studio 17 2022"
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `[doctest] Status: SUCCESS!` and `test cases: 1 | 1 passed`.

- [ ] **Step 0.8: Commit**

```bash
git add .gitignore test/host core/include/rsvp/token.hpp
git commit -m "feat(core): host test skeleton + token model"
```

---

### Task 1: ORP pivot (UTF-8 aware)

**Files:**
- Create: `core/include/rsvp/utf8.hpp`
- Create: `core/include/rsvp/orp.hpp`
- Create: `core/src/orp.cpp`
- Create: `test/host/test_orp.cpp`
- Modify: `test/host/CMakeLists.txt`

- [ ] **Step 1.1: Add the UTF-8 helper (header-only)**

Create `core/include/rsvp/utf8.hpp`:
```cpp
#pragma once
#include <cstddef>
#include <string>

namespace rsvp { namespace utf8 {

inline bool isContinuation(unsigned char c) { return (c & 0xC0u) == 0x80u; }

// Number of UTF-8 code points in s.
inline std::size_t length(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) if (!isContinuation(c)) ++n;
    return n;
}

// Byte offset where code point #cpIndex begins; s.size() if cpIndex >= length(s).
inline std::size_t byteOffset(const std::string& s, std::size_t cpIndex) {
    std::size_t cp = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (!isContinuation(static_cast<unsigned char>(s[i]))) {
            if (cp == cpIndex) return i;
            ++cp;
        }
    }
    return s.size();
}

}} // namespace rsvp::utf8
```

- [ ] **Step 1.2: Declare the ORP API and a failing stub**

Create `core/include/rsvp/orp.hpp`:
```cpp
#pragma once
#include <cstddef>
#include <string>

namespace rsvp {

// 0-based code-point index of the Optimal Recognition Point letter.
std::size_t orpIndex(const std::string& word);

// The word cut into the part before the ORP letter, the ORP letter, and the rest.
struct OrpSplit { std::string pre, orp, post; };
OrpSplit orpSplit(const std::string& word);

} // namespace rsvp
```

Create `core/src/orp.cpp` with a deliberately wrong stub (so tests compile + fail):
```cpp
#include "rsvp/orp.hpp"
#include "rsvp/utf8.hpp"

namespace rsvp {

std::size_t orpIndex(const std::string&) { return 999; }     // STUB - wrong on purpose
OrpSplit    orpSplit(const std::string&) { return {}; }      // STUB

} // namespace rsvp
```

- [ ] **Step 1.3: Write the failing tests**

Create `test/host/test_orp.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/orp.hpp"
using namespace rsvp;

TEST_CASE("orpIndex pivots by length bucket") {
    CHECK(orpIndex("")              == 0);
    CHECK(orpIndex("a")             == 0);
    CHECK(orpIndex("cat")           == 0);  // 1-3 -> 1st letter
    CHECK(orpIndex("read")          == 1);  // 4-5 -> 2nd
    CHECK(orpIndex("words")         == 1);
    CHECK(orpIndex("reading")       == 2);  // 6-9 -> 3rd
    CHECK(orpIndex("wonderful")     == 2);
    CHECK(orpIndex("incredible")    == 3);  // 10+ -> 4th
    CHECK(orpIndex("extraordinary") == 3);
}

TEST_CASE("orpSplit cuts around the pivot letter") {
    auto a = orpSplit("reading");
    CHECK(a.pre == "re"); CHECK(a.orp == "a"); CHECK(a.post == "ding");

    auto b = orpSplit("cat");
    CHECK(b.pre == "");   CHECK(b.orp == "c"); CHECK(b.post == "at");

    auto c = orpSplit("read");
    CHECK(c.pre == "r");  CHECK(c.orp == "e"); CHECK(c.post == "ad");

    auto e = orpSplit("");
    CHECK(e.pre == "");   CHECK(e.orp == "");  CHECK(e.post == "");
}

TEST_CASE("orpSplit respects UTF-8 code points") {
    auto s = orpSplit("caf\xC3\xA9"); // "café" (é = U+00E9), 4 code points
    CHECK(s.pre  == "c");
    CHECK(s.orp  == "a");
    CHECK(s.post == "f\xC3\xA9");
}
```

- [ ] **Step 1.4: Wire the new sources into CMake**

In `test/host/CMakeLists.txt`, replace the `add_executable(rsvp_tests ...)` block with:
```cmake
add_executable(rsvp_tests
    test_main.cpp
    test_token.cpp
    test_orp.cpp
    ${CORE_DIR}/src/orp.cpp
)
```

- [ ] **Step 1.5: Build + run → FAIL**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `[doctest] Status: FAILURE!` — `orpIndex("")` etc. report the stub's `999`, and the `orpSplit` checks fail with empty strings.

- [ ] **Step 1.6: Implement ORP for real**

Replace the body of `core/src/orp.cpp`:
```cpp
#include "rsvp/orp.hpp"
#include "rsvp/utf8.hpp"

namespace rsvp {

std::size_t orpIndex(const std::string& word) {
    const std::size_t L = utf8::length(word);
    if (L == 0) return 0;
    std::size_t p;
    if      (L <= 3) p = 0;
    else if (L <= 5) p = 1;
    else if (L <= 9) p = 2;
    else             p = 3;
    if (p >= L) p = L - 1;   // clamp (defensive; buckets keep p < L for L >= 1)
    return p;
}

OrpSplit orpSplit(const std::string& word) {
    const std::size_t p = orpIndex(word);
    const std::size_t a = utf8::byteOffset(word, p);
    const std::size_t b = utf8::byteOffset(word, p + 1);
    OrpSplit s;
    s.pre  = word.substr(0, a);
    s.orp  = word.substr(a, b - a);
    s.post = word.substr(b);
    return s;
}

} // namespace rsvp
```

- [ ] **Step 1.7: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `[doctest] Status: SUCCESS!`, `test cases: 3 | 3 passed`.

- [ ] **Step 1.8: Commit**

```bash
git add core/include/rsvp/utf8.hpp core/include/rsvp/orp.hpp core/src/orp.cpp \
        test/host/test_orp.cpp test/host/CMakeLists.txt
git commit -m "feat(core): UTF-8-aware ORP pivot"
```

---

### Task 2: Pacing / per-word timing

**Files:**
- Create: `core/include/rsvp/pacing.hpp`
- Create: `core/src/pacing.cpp`
- Create: `test/host/test_pacing.cpp`
- Modify: `test/host/CMakeLists.txt`

- [ ] **Step 2.1: Declare the pacing API and a failing stub**

Create `core/include/rsvp/pacing.hpp`:
```cpp
#pragma once
#include "rsvp/token.hpp"

namespace rsvp {

struct PacingConfig {
    int    wpm                = 300;   // base words per minute
    double sentenceEndFactor  = 2.0;   // duration multiplier at sentence end
    double paragraphEndFactor = 2.5;   // at paragraph end
    double chapterStartFactor = 3.0;   // at chapter start
    int    longWordThreshold  = 8;     // chars beyond which the per-char bonus applies
    double longWordPerCharMs  = 0.0;   // ms added per char beyond the threshold
    int    minWordMs          = 60;    // clamp floor
};

// Base ms per word from WPM (wpm clamped to >= 1).
int baseWordMs(int wpm);

// Full display duration for a token given its flags + length.
int wordDurationMs(const Token& tok, const PacingConfig& cfg);

} // namespace rsvp
```

Create `core/src/pacing.cpp` with wrong stubs:
```cpp
#include "rsvp/pacing.hpp"

namespace rsvp {

int baseWordMs(int)                              { return -1; } // STUB
int wordDurationMs(const Token&, const PacingConfig&) { return -1; } // STUB

} // namespace rsvp
```

- [ ] **Step 2.2: Write the failing tests**

Create `test/host/test_pacing.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/pacing.hpp"
using namespace rsvp;

TEST_CASE("baseWordMs from WPM") {
    CHECK(baseWordMs(300) == 200);
    CHECK(baseWordMs(600) == 100);
    CHECK(baseWordMs(100) == 600);
    CHECK(baseWordMs(0)   == 60000); // guarded: wpm clamped to 1
}

TEST_CASE("wordDurationMs applies flag factors") {
    PacingConfig cfg; // wpm 300 -> 200 ms base
    CHECK(wordDurationMs(Token{"word", FLAG_NONE},          cfg) == 200);
    CHECK(wordDurationMs(Token{"end.", FLAG_SENTENCE_END},  cfg) == 400);
    CHECK(wordDurationMs(Token{"para", FLAG_PARAGRAPH_END}, cfg) == 500);
    CHECK(wordDurationMs(Token{"Chap", FLAG_CHAPTER_START}, cfg) == 600);
}

TEST_CASE("wordDurationMs uses the strongest factor when multiple flags set") {
    PacingConfig cfg;
    Token t{"done.", static_cast<std::uint8_t>(FLAG_SENTENCE_END | FLAG_PARAGRAPH_END)};
    CHECK(wordDurationMs(t, cfg) == 500); // max(2.0, 2.5) * 200
}

TEST_CASE("wordDurationMs adds a long-word bonus when enabled") {
    PacingConfig cfg;
    cfg.longWordPerCharMs = 10.0;
    cfg.longWordThreshold = 8;
    CHECK(wordDurationMs(Token{"extraordinary", FLAG_NONE}, cfg) == 250); // 200 + (13-8)*10
}

TEST_CASE("wordDurationMs clamps to the minimum") {
    PacingConfig cfg;
    cfg.wpm = 2000; // base 30 ms
    CHECK(wordDurationMs(Token{"x", FLAG_NONE}, cfg) == 60);
}
```

- [ ] **Step 2.3: Wire into CMake**

In `test/host/CMakeLists.txt`, replace the `add_executable(rsvp_tests ...)` block with:
```cmake
add_executable(rsvp_tests
    test_main.cpp
    test_token.cpp
    test_orp.cpp
    test_pacing.cpp
    ${CORE_DIR}/src/orp.cpp
    ${CORE_DIR}/src/pacing.cpp
)
```

- [ ] **Step 2.4: Build + run → FAIL**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `[doctest] Status: FAILURE!` — pacing checks get `-1`.

- [ ] **Step 2.5: Implement pacing**

Replace the body of `core/src/pacing.cpp`:
```cpp
#include "rsvp/pacing.hpp"
#include "rsvp/utf8.hpp"
#include <algorithm>

namespace rsvp {

int baseWordMs(int wpm) {
    const int w = wpm < 1 ? 1 : wpm;
    return 60000 / w;
}

int wordDurationMs(const Token& tok, const PacingConfig& cfg) {
    double ms = baseWordMs(cfg.wpm);

    const int len = static_cast<int>(utf8::length(tok.text));
    if (cfg.longWordPerCharMs > 0.0 && len > cfg.longWordThreshold)
        ms += (len - cfg.longWordThreshold) * cfg.longWordPerCharMs;

    double factor = 1.0;
    if (tok.has(FLAG_SENTENCE_END))  factor = std::max(factor, cfg.sentenceEndFactor);
    if (tok.has(FLAG_PARAGRAPH_END)) factor = std::max(factor, cfg.paragraphEndFactor);
    if (tok.has(FLAG_CHAPTER_START)) factor = std::max(factor, cfg.chapterStartFactor);
    ms *= factor;

    int out = static_cast<int>(ms + 0.5);
    if (out < cfg.minWordMs) out = cfg.minWordMs;
    return out;
}

} // namespace rsvp
```

- [ ] **Step 2.6: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `[doctest] Status: SUCCESS!`, `test cases: 8 | 8 passed`.

- [ ] **Step 2.7: Commit**

```bash
git add core/include/rsvp/pacing.hpp core/src/pacing.cpp \
        test/host/test_pacing.cpp test/host/CMakeLists.txt
git commit -m "feat(core): WPM pacing with flag + long-word modifiers"
```

---

### Task 3: Player state machine (play / pause / tick / seek / progress)

**Files:**
- Create: `core/include/rsvp/player.hpp`
- Create: `core/src/player.cpp`
- Create: `test/host/test_player.cpp`
- Modify: `test/host/CMakeLists.txt`

- [ ] **Step 3.1: Declare the Player API and a failing stub**

Create `core/include/rsvp/player.hpp`:
```cpp
#pragma once
#include "rsvp/token.hpp"
#include "rsvp/pacing.hpp"
#include <cstddef>

namespace rsvp {

// Drives playback over a Document. Time is supplied externally via tick(dtMs),
// so it is fully deterministic and testable with no real clock.
class Player {
public:
    Player(const Document& doc, PacingConfig cfg);

    void play();
    void pause();
    void togglePlay();

    bool isPlaying()  const { return playing_; }
    bool isFinished() const { return finished_; }

    std::size_t  index() const { return index_; }
    std::size_t  size()  const { return doc_.size(); }
    const Token& current() const { return doc_.tokens[index_]; } // precondition: !empty
    double       progress() const;

    // Advance the clock by dtMs. While playing, advances through any tokens whose
    // duration has elapsed. Returns the number of tokens advanced past.
    int  tick(int dtMs);

    void seek(std::size_t i);   // clamps to [0, size-1]; resets the word timer
    void nextSentence();        // jump to the start of the following sentence
    void prevSentence();        // jump to the start of the current/previous sentence

private:
    const Document& doc_;
    PacingConfig    cfg_;
    std::size_t     index_    = 0;
    int             elapsed_  = 0;
    bool            playing_  = false;
    bool            finished_ = false;
};

} // namespace rsvp
```

Create `core/src/player.cpp` with wrong stubs (compiles, fails):
```cpp
#include "rsvp/player.hpp"

namespace rsvp {

Player::Player(const Document& doc, PacingConfig cfg) : doc_(doc), cfg_(cfg) {}

void   Player::play()        {}
void   Player::pause()       {}
void   Player::togglePlay()  {}
double Player::progress() const { return -1.0; }     // STUB
int    Player::tick(int)     { return -1; }          // STUB
void   Player::seek(std::size_t) {}
void   Player::nextSentence() {}
void   Player::prevSentence() {}

} // namespace rsvp
```

- [ ] **Step 3.2: Write the failing tests**

Create `test/host/test_player.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/player.hpp"
#include <initializer_list>
using namespace rsvp;

static Document plainDoc(std::initializer_list<const char*> words) {
    Document d;
    for (auto w : words) d.tokens.push_back(Token{w, FLAG_NONE});
    return d;
}

TEST_CASE("Player initial state") {
    Document d = plainDoc({"one", "two", "three"});
    Player p(d, PacingConfig{}); // 200 ms/word
    CHECK(p.index() == 0);
    CHECK(p.size() == 3);
    CHECK_FALSE(p.isPlaying());
    CHECK_FALSE(p.isFinished());
    CHECK(p.current().text == "one");
}

TEST_CASE("Player advances on tick while playing") {
    Document d = plainDoc({"one", "two", "three"});
    Player p(d, PacingConfig{});
    p.play();
    CHECK(p.isPlaying());
    CHECK(p.tick(199) == 0);
    CHECK(p.index() == 0);
    CHECK(p.tick(1) == 1);          // total 200 -> advance one
    CHECK(p.index() == 1);
    CHECK(p.current().text == "two");
}

TEST_CASE("Player finishes after the last word completes") {
    Document d = plainDoc({"one", "two", "three"});
    Player p(d, PacingConfig{});
    p.play();
    CHECK(p.tick(200) == 1); CHECK(p.index() == 1);
    CHECK(p.tick(200) == 1); CHECK(p.index() == 2);
    CHECK(p.tick(200) == 0);        // completing the last word finishes
    CHECK(p.index() == 2);
    CHECK(p.isFinished());
    CHECK_FALSE(p.isPlaying());
}

TEST_CASE("Player pause halts advance and resumes continuously") {
    Document d = plainDoc({"one", "two", "three"});
    Player p(d, PacingConfig{});
    p.play();
    CHECK(p.tick(100) == 0);
    p.pause();
    CHECK_FALSE(p.isPlaying());
    CHECK(p.tick(1000) == 0);       // paused: no advance
    CHECK(p.index() == 0);
    p.play();
    CHECK(p.tick(100) == 1);        // 100 + 100 = 200 -> advance
    CHECK(p.index() == 1);
}

TEST_CASE("Player seek clamps and resets the timer") {
    Document d = plainDoc({"one", "two", "three"});
    Player p(d, PacingConfig{});
    p.seek(99);
    CHECK(p.index() == 2);
    p.seek(1);
    CHECK(p.index() == 1);
    p.play();
    CHECK(p.tick(199) == 0);        // timer was reset by seek
    CHECK(p.tick(1) == 1);
    CHECK(p.index() == 2);
}

TEST_CASE("Player progress") {
    Document d = plainDoc({"one", "two", "three"});
    Player p(d, PacingConfig{});
    CHECK(p.progress() == doctest::Approx(0.0));
    p.seek(1);
    CHECK(p.progress() == doctest::Approx(1.0 / 3.0));
    p.seek(2);
    p.play();
    p.tick(200);                    // completes last word -> finished
    CHECK(p.isFinished());
    CHECK(p.progress() == doctest::Approx(1.0));
}

TEST_CASE("Player with an empty document is finished and inert") {
    Document d;
    Player p(d, PacingConfig{});
    CHECK(p.isFinished());
    p.play();
    CHECK_FALSE(p.isPlaying());
    CHECK(p.tick(1000) == 0);
}
```

- [ ] **Step 3.3: Wire into CMake**

In `test/host/CMakeLists.txt`, replace the `add_executable(rsvp_tests ...)` block with:
```cmake
add_executable(rsvp_tests
    test_main.cpp
    test_token.cpp
    test_orp.cpp
    test_pacing.cpp
    test_player.cpp
    ${CORE_DIR}/src/orp.cpp
    ${CORE_DIR}/src/pacing.cpp
    ${CORE_DIR}/src/player.cpp
)
```

- [ ] **Step 3.4: Build + run → FAIL**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `[doctest] Status: FAILURE!` — the stub returns make the player checks fail.

- [ ] **Step 3.5: Implement the Player**

Replace the body of `core/src/player.cpp`:
```cpp
#include "rsvp/player.hpp"

namespace rsvp {

Player::Player(const Document& doc, PacingConfig cfg)
    : doc_(doc), cfg_(cfg), finished_(doc.empty()) {}

void Player::play()       { if (!doc_.empty() && !finished_) playing_ = true; }
void Player::pause()      { playing_ = false; }
void Player::togglePlay() { if (playing_) pause(); else play(); }

double Player::progress() const {
    if (doc_.empty()) return 0.0;
    if (finished_)    return 1.0;
    return static_cast<double>(index_) / static_cast<double>(doc_.size());
}

int Player::tick(int dtMs) {
    if (!playing_ || finished_ || doc_.empty() || dtMs <= 0) return 0;
    elapsed_ += dtMs;
    int advanced = 0;
    while (true) {
        const int dur = wordDurationMs(doc_.tokens[index_], cfg_);
        if (elapsed_ < dur) break;
        elapsed_ -= dur;
        if (index_ + 1 >= doc_.size()) {       // last word completed
            finished_ = true; playing_ = false; elapsed_ = 0;
            break;
        }
        ++index_; ++advanced;
    }
    return advanced;
}

void Player::seek(std::size_t i) {
    if (doc_.empty()) return;
    if (i >= doc_.size()) i = doc_.size() - 1;
    index_ = i; elapsed_ = 0; finished_ = false;
}

void Player::nextSentence() {
    if (doc_.empty()) return;
    std::size_t i = index_;
    for (; i < doc_.size(); ++i)
        if (doc_.tokens[i].has(FLAG_SENTENCE_END)) break;
    if (i < doc_.size())
        seek(i + 1 < doc_.size() ? i + 1 : doc_.size() - 1);
    else
        seek(doc_.size() - 1);
}

void Player::prevSentence() {
    if (doc_.empty()) return;
    const std::size_t cur = index_;
    std::size_t s = 0;                          // start of the current sentence
    for (std::size_t i = cur; i-- > 0; )
        if (doc_.tokens[i].has(FLAG_SENTENCE_END)) { s = i + 1; break; }
    if (s < cur) { seek(s); return; }           // mid-sentence -> jump to its start
    if (s == 0)  { seek(0); return; }           // already at first sentence start
    std::size_t ps = 0;                         // start of the previous sentence
    for (std::size_t i = s - 1; i-- > 0; )
        if (doc_.tokens[i].has(FLAG_SENTENCE_END)) { ps = i + 1; break; }
    seek(ps);
}

} // namespace rsvp
```

- [ ] **Step 3.6: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `[doctest] Status: SUCCESS!`, `test cases: 15 | 15 passed`.

- [ ] **Step 3.7: Commit**

```bash
git add core/include/rsvp/player.hpp core/src/player.cpp \
        test/host/test_player.cpp test/host/CMakeLists.txt
git commit -m "feat(core): deterministic playback state machine"
```

---

### Task 4: Sentence navigation tests

The `nextSentence` / `prevSentence` logic was implemented in Task 3; this task adds explicit coverage for it (no production-code change unless a test fails).

**Files:**
- Modify: `test/host/test_player.cpp`

- [ ] **Step 4.1: Add navigation tests**

Append to `test/host/test_player.cpp`:
```cpp
// Sentences: [The cat sat.] [It ran.] [End]
static Document sentenceDoc() {
    Document d;
    d.tokens = {
        {"The",  FLAG_NONE}, {"cat", FLAG_NONE}, {"sat.", FLAG_SENTENCE_END},
        {"It",   FLAG_NONE}, {"ran.", FLAG_SENTENCE_END},
        {"End",  FLAG_NONE},
    };
    return d;
}

TEST_CASE("nextSentence jumps to the start of the following sentence") {
    Document d = sentenceDoc();
    Player p(d, PacingConfig{});
    p.nextSentence(); CHECK(p.index() == 3);  // after "sat."
    p.nextSentence(); CHECK(p.index() == 5);  // after "ran."
    p.nextSentence(); CHECK(p.index() == 5);  // none left -> clamps to last
}

TEST_CASE("prevSentence goes to current start, then previous start") {
    Document d = sentenceDoc();
    Player p(d, PacingConfig{});
    p.seek(4); p.prevSentence(); CHECK(p.index() == 3); // start of current sentence
    p.prevSentence();            CHECK(p.index() == 0); // previous sentence start
    p.prevSentence();            CHECK(p.index() == 0); // clamp at first
    p.seek(5); p.prevSentence(); CHECK(p.index() == 3); // from "End" -> sentence 2 start
}
```

- [ ] **Step 4.2: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `[doctest] Status: SUCCESS!`, `test cases: 17 | 17 passed`. If either navigation test fails, fix `nextSentence`/`prevSentence` in `core/src/player.cpp` to satisfy the documented behavior, then re-run.

- [ ] **Step 4.3: Commit**

```bash
git add test/host/test_player.cpp
git commit -m "test(core): sentence navigation coverage"
```

---

## Self-review (performed against the spec)

**1. Spec coverage (this plan's slice):**
- ORP algorithm (spec §8) → Task 1 (`orpIndex`/`orpSplit`, exact length buckets 1-3/4-5/6-9/10+). ✓
- Pacing: base WPM + sentence/paragraph/chapter pauses + long-word bonus + floor (spec §8) → Task 2. ✓
- Playback (play/pause, deterministic timing, seek, sentence nav, progress) (spec §8 gestures map onto these) → Tasks 3–4. ✓
- Token model with sentence/paragraph/chapter flags that the compiled stream will carry (spec §6) → Task 0. ✓
- Host-test-first, hardware-independent core (spec §4, §12) → entire plan. ✓
- *Deferred by design:* tokenizer/indexers, index codec, UI, gestures-to-input wiring, HAL — named in Scope as Plans 2–7. No silent gaps.

**2. Placeholder scan:** No "TBD/TODO/handle appropriately". Stubs are explicit, intentional red-state code that the very next step replaces. Every code step shows complete code. ✓

**3. Type consistency:** `Token{text, flags}`, `TokenFlags` names, `Document::tokens/size/empty`, `orpIndex`/`orpSplit`/`OrpSplit{pre,orp,post}`, `PacingConfig` fields, `baseWordMs`/`wordDurationMs`, and the `Player` method set (`play/pause/togglePlay/isPlaying/isFinished/index/size/current/progress/tick/seek/nextSentence/prevSentence`) are used identically in headers, sources, and tests. `utf8::length`/`utf8::byteOffset` signatures match call sites in `orp.cpp` and `pacing.cpp`. ✓

---

## Execution handoff

Implement task-by-task with `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans`. After Task 4, the engine core is complete and fully tested; **Plan 2 (TXT tokenizer + compiled-index codec)** builds directly on this `core/` library.

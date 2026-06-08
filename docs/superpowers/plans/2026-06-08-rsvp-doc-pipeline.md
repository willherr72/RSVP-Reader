# RSVP Document Pipeline (TXT) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the host-tested content pipeline that turns plain UTF-8 text into the engine's `Document` and (de)serializes it to the compiled-index byte format with sparse random access — the bridge between a raw file and the RSVP engine.

**Architecture:** Two pure-logic units added to the existing `core/` library: a **tokenizer** (`text → Document`, marking sentence/paragraph flags) and an **index codec** (`Document + metadata ↔ bytes`, plus a sparse seek table for O(seek-interval) random access and a cache-invalidation check). No filesystem, no hardware — actual SD/file I/O is deferred to the ESP-IDF bring-up plan. Same host harness as Plan 1 (doctest + MSVC/VS 2022 generator). Strict TDD.

**Tech Stack:** C++17, CMake with the Visual Studio 2022 (MSVC) generator, doctest v2.4.11. Builds on Plan 1's `rsvp::Token`/`Document`/`TokenFlags`/`utf8`.

---

## Scope & rationale

Per the design spec (§4–6), every format-specific indexer emits the same **uniform compiled token stream** that the engine consumes; this plan implements that stream for **plain text** and the binary codec for it. It is deliberately filesystem-free pure logic so it unit-tests on a host now and compiles unchanged into the firmware later.

**In scope:** plain-text tokenizer; the compiled-index binary format (serialize + parse); sparse seek table for random access; cache-invalidation check.
**Out of scope (later plans):** SD/FATFS file reads & the on-disk `/.rsvp/` cache, cover thumbnails, the EPUB indexer (Plan: EPUB), title-from-filename and mtime/size acquisition (these come from the ESP-IDF file layer that *calls* `serializeIndex`).

**Dependency / branch:** This plan depends on Plan 1's `core/`, which currently lives only on `feature/rsvp-engine-core` (not `main`). Execution must branch **`feature/rsvp-doc-pipeline` off `feature/rsvp-engine-core`**.

## Design decisions (please review before execution)

1. **Tokenizer — word splitting:** split on ASCII whitespace (space, tab, `\n`, `\r`, `\f`, `\v`). Punctuation stays attached to the word (so `"sat."` is one token carrying `FLAG_SENTENCE_END`). This is UTF-8-safe (multibyte continuation bytes are ≥ 0x80, never whitespace).
2. **Tokenizer — sentence end:** a word ends a sentence if, after stripping trailing closing characters `" ' ) ] }`, it ends with `.`, `!`, or `?`. Simple and best-effort (a false positive after `Mr.` just adds a slightly longer pause — harmless). No abbreviation dictionary.
3. **Tokenizer — paragraph end:** a whitespace gap containing **2+ newlines** (a blank line) sets `FLAG_PARAGRAPH_END` on the word **before** the gap.
4. **Tokenizer — chapters:** plain text has none, so the tokenizer never sets `FLAG_CHAPTER_START`; the TXT indexer will pass an empty chapter list. (EPUB supplies real chapters later.)
5. **Index format:** little-endian (the ESP32-S3 is LE), fixed header + variable sections, `uint16` word lengths (≤ 65535 bytes/word), `uint32` counts/offsets. Magic `"RSVI"`, version `1`. A **sparse seek table** stores the byte offset of every *seek-interval*-th word (interval is a serialize parameter, default 256, stored in the header) for fast resume/jump. Layout: `header → chapter table → seek table → token stream`; seek offsets are relative to the token-stream section.
6. **Metadata source:** `serializeIndex` takes a caller-provided `DocMeta` (title/author/sourceSize/sourceMtime). Deriving title-from-filename and reading file size/mtime belongs to the ESP-IDF file layer, not here.
7. **No exceptions:** parsing reports failure via an `ok()` flag (not `throw`), matching the engine core's exception-free, MCU-friendly style.

## File structure

```
core/include/rsvp/tokenize.hpp  — text -> Document API
core/src/tokenize.cpp           — tokenizer impl (+ file-local endsSentence)
core/include/rsvp/byteio.hpp    — little-endian put/get helpers (header-only)
core/include/rsvp/index.hpp     — DocMeta, Chapter, serializeIndex, CompiledIndex, indexMatchesSource
core/src/index.cpp              — codec impl
test/host/test_tokenize.cpp     — tokenizer tests
test/host/test_byteio.cpp       — byte-helper tests
test/host/test_index.cpp        — codec tests (serialize / round-trip / seek / invalidation)
test/host/CMakeLists.txt        — MODIFIED: add the new sources + tests
```

## Conventions

- **TDD:** failing test → watch it fail → minimal impl → watch it pass → commit.
- **Toolchain:** the `build/host/` dir is already configured (Plan 1) with `-G "Visual Studio 17 2022"`. Build with `cmake --build build/host` (defaults to Debug; auto-reconfigures when `CMakeLists.txt` changes). Run `./build/host/Debug/rsvp_tests.exe`. If `build/host` is somehow missing, reconfigure: `cmake -S test/host -B build/host -G "Visual Studio 17 2022"`.
- **Commit trailer:** end every commit message with `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
- **Branch:** `feature/rsvp-doc-pipeline` off `feature/rsvp-engine-core` (created at execution time). Do not implement on `main`.
- **Counts:** the suite starts at **21** cases (Plan 1). The "Expected" counts below assume that baseline; the key signal is **`Status: SUCCESS!` with 0 failed** — if a count is off by a small amount but 0 fail, that's fine.

---

### Task 0: Plain-text tokenizer

**Files:**
- Create: `core/include/rsvp/tokenize.hpp`
- Create: `core/src/tokenize.cpp`
- Create: `test/host/test_tokenize.cpp`
- Modify: `test/host/CMakeLists.txt`

- [ ] **Step 0.1: Declare the API + a failing stub**

Create `core/include/rsvp/tokenize.hpp`:
```cpp
#pragma once
#include "rsvp/token.hpp"
#include <string>

namespace rsvp {

// Split plain UTF-8 text into a Document. Words are whitespace-delimited (punctuation
// stays attached). FLAG_SENTENCE_END is set on words ending a sentence; FLAG_PARAGRAPH_END
// on the last word before a blank line. (Plain text has no chapters -> no FLAG_CHAPTER_START.)
Document tokenizePlainText(const std::string& text);

} // namespace rsvp
```

Create `core/src/tokenize.cpp` as a STUB (compiles, fails the tests):
```cpp
#include "rsvp/tokenize.hpp"

namespace rsvp {

Document tokenizePlainText(const std::string&) { return Document{}; } // STUB

} // namespace rsvp
```

- [ ] **Step 0.2: Write the failing tests**

Create `test/host/test_tokenize.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/tokenize.hpp"
using namespace rsvp;

TEST_CASE("tokenize: empty / whitespace-only text yields no tokens") {
    CHECK(tokenizePlainText("").empty());
    CHECK(tokenizePlainText("   \n\t  ").empty());
}

TEST_CASE("tokenize: splits on whitespace, keeps punctuation attached") {
    Document d = tokenizePlainText("The cat sat.");
    REQUIRE(d.size() == 3);
    CHECK(d.tokens[0].text == "The");
    CHECK(d.tokens[1].text == "cat");
    CHECK(d.tokens[2].text == "sat.");
}

TEST_CASE("tokenize: marks sentence ends (. ! ?)") {
    Document d = tokenizePlainText("Hi there. Go now! Really?");
    REQUIRE(d.size() == 5);
    CHECK_FALSE(d.tokens[0].has(FLAG_SENTENCE_END)); // Hi
    CHECK(d.tokens[1].has(FLAG_SENTENCE_END));       // there.
    CHECK_FALSE(d.tokens[2].has(FLAG_SENTENCE_END)); // Go
    CHECK(d.tokens[3].has(FLAG_SENTENCE_END));       // now!
    CHECK(d.tokens[4].has(FLAG_SENTENCE_END));       // Really?
}

TEST_CASE("tokenize: sentence end allows trailing quotes/brackets") {
    Document d = tokenizePlainText("He said \"go.\" (Yes.)");
    REQUIRE(d.size() == 4);
    CHECK(d.tokens[2].has(FLAG_SENTENCE_END));       // "go."
    CHECK(d.tokens[3].has(FLAG_SENTENCE_END));       // (Yes.)
}

TEST_CASE("tokenize: blank line marks paragraph end on the preceding word") {
    Document d = tokenizePlainText("First para end.\n\nSecond para.");
    REQUIRE(d.size() == 5);
    CHECK(d.tokens[2].has(FLAG_PARAGRAPH_END));       // end. (blank line follows)
    CHECK(d.tokens[2].has(FLAG_SENTENCE_END));        // also a sentence end
    CHECK_FALSE(d.tokens[4].has(FLAG_PARAGRAPH_END)); // last word, no trailing blank line
}

TEST_CASE("tokenize: a single newline is not a paragraph break") {
    Document d = tokenizePlainText("line one\nline two");
    REQUIRE(d.size() == 4);
    CHECK_FALSE(d.tokens[1].has(FLAG_PARAGRAPH_END)); // "one"
}
```

- [ ] **Step 0.3: Wire into CMake**

In `test/host/CMakeLists.txt`, replace the `add_executable(rsvp_tests ...)` block with:
```cmake
add_executable(rsvp_tests
    test_main.cpp
    test_token.cpp
    test_orp.cpp
    test_pacing.cpp
    test_player.cpp
    test_tokenize.cpp
    ${CORE_DIR}/src/orp.cpp
    ${CORE_DIR}/src/pacing.cpp
    ${CORE_DIR}/src/player.cpp
    ${CORE_DIR}/src/tokenize.cpp
)
```

- [ ] **Step 0.4: Build + run → FAIL**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: FAILURE!` — the 6 tokenize cases fail (stub returns an empty Document, so the `REQUIRE(d.size()==…)` checks fail). Confirm FAILURE before continuing.

- [ ] **Step 0.5: Implement the tokenizer**

Replace the body of `core/src/tokenize.cpp`:
```cpp
#include "rsvp/tokenize.hpp"

namespace rsvp {

namespace {

bool isSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// A word ends a sentence if, after stripping trailing closing quotes/brackets,
// it ends with '.', '!' or '?'.
bool endsSentence(const std::string& w) {
    std::size_t e = w.size();
    auto isCloser = [](char c) {
        return c == '"' || c == '\'' || c == ')' || c == ']' || c == '}';
    };
    while (e > 0 && isCloser(w[e - 1])) --e;
    if (e == 0) return false;
    const char c = w[e - 1];
    return c == '.' || c == '!' || c == '?';
}

} // namespace

Document tokenizePlainText(const std::string& text) {
    Document doc;
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        // Skip whitespace; count newlines to detect blank-line (paragraph) breaks.
        int newlines = 0;
        while (i < n && isSpace(static_cast<unsigned char>(text[i]))) {
            if (text[i] == '\n') ++newlines;
            ++i;
        }
        // A blank line in the gap ends the paragraph of the previous word.
        if (newlines >= 2 && !doc.tokens.empty())
            doc.tokens.back().flags |= FLAG_PARAGRAPH_END;
        if (i >= n) break;
        // Read one word (maximal run of non-whitespace).
        const std::size_t start = i;
        while (i < n && !isSpace(static_cast<unsigned char>(text[i]))) ++i;
        std::string word = text.substr(start, i - start);
        std::uint8_t flags = FLAG_NONE;
        if (endsSentence(word)) flags |= FLAG_SENTENCE_END;
        doc.tokens.push_back(Token{word, flags});
    }
    return doc;
}

} // namespace rsvp
```

- [ ] **Step 0.6: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed (cumulative **27 test cases**: 21 + 6 tokenize). Paste the summary lines.

- [ ] **Step 0.7: Commit**

```bash
git add core/include/rsvp/tokenize.hpp core/src/tokenize.cpp test/host/test_tokenize.cpp test/host/CMakeLists.txt
git commit -m "feat(core): plain-text tokenizer with sentence/paragraph flags" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 1: Little-endian byte I/O helpers

**Files:**
- Create: `core/include/rsvp/byteio.hpp`
- Create: `test/host/test_byteio.cpp`
- Modify: `test/host/CMakeLists.txt`

- [ ] **Step 1.1: Write the helper (header-only)**

Create `core/include/rsvp/byteio.hpp`:
```cpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace rsvp { namespace byteio {

// Append little-endian integers to a byte buffer.
inline void putU16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
inline void putU32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

// Read little-endian integers starting at off, advancing off. Caller bounds-checks.
inline std::uint16_t getU16(const std::vector<std::uint8_t>& b, std::size_t& off) {
    const std::uint16_t v = static_cast<std::uint16_t>(b[off])
                          | (static_cast<std::uint16_t>(b[off + 1]) << 8);
    off += 2;
    return v;
}
inline std::uint32_t getU32(const std::vector<std::uint8_t>& b, std::size_t& off) {
    const std::uint32_t v = static_cast<std::uint32_t>(b[off])
                          | (static_cast<std::uint32_t>(b[off + 1]) << 8)
                          | (static_cast<std::uint32_t>(b[off + 2]) << 16)
                          | (static_cast<std::uint32_t>(b[off + 3]) << 24);
    off += 4;
    return v;
}

}} // namespace rsvp::byteio
```

- [ ] **Step 1.2: Write the failing tests**

Create `test/host/test_byteio.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/byteio.hpp"
using namespace rsvp;

TEST_CASE("byteio: put/get round-trips u16 and u32") {
    std::vector<std::uint8_t> b;
    byteio::putU16(b, 0xABCD);
    byteio::putU32(b, 0x12345678u);
    std::size_t off = 0;
    CHECK(byteio::getU16(b, off) == 0xABCD);
    CHECK(off == 2);
    CHECK(byteio::getU32(b, off) == 0x12345678u);
    CHECK(off == 6);
}

TEST_CASE("byteio: writes little-endian byte order") {
    std::vector<std::uint8_t> b;
    byteio::putU16(b, 0x00FF);          // -> FF 00
    byteio::putU32(b, 0x000000FFu);     // -> FF 00 00 00
    CHECK(b[0] == 0xFF); CHECK(b[1] == 0x00);
    CHECK(b[2] == 0xFF); CHECK(b[3] == 0x00); CHECK(b[4] == 0x00); CHECK(b[5] == 0x00);
}
```

- [ ] **Step 1.3: Wire into CMake**

In `test/host/CMakeLists.txt`, replace the `add_executable(rsvp_tests ...)` block with:
```cmake
add_executable(rsvp_tests
    test_main.cpp
    test_token.cpp
    test_orp.cpp
    test_pacing.cpp
    test_player.cpp
    test_tokenize.cpp
    test_byteio.cpp
    ${CORE_DIR}/src/orp.cpp
    ${CORE_DIR}/src/pacing.cpp
    ${CORE_DIR}/src/player.cpp
    ${CORE_DIR}/src/tokenize.cpp
)
```

- [ ] **Step 1.4: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed (cumulative **29 test cases**). The byteio helpers are correct on first write, so these tests pass immediately — that is acceptable for a pure, self-evident utility (the value is the regression guard + pinning little-endian order). Paste the summary lines.

- [ ] **Step 1.5: Commit**

```bash
git add core/include/rsvp/byteio.hpp test/host/test_byteio.cpp test/host/CMakeLists.txt
git commit -m "feat(core): little-endian byte I/O helpers" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Compiled-index serialization

**Files:**
- Create: `core/include/rsvp/index.hpp`
- Create: `core/src/index.cpp`
- Create: `test/host/test_index.cpp`
- Modify: `test/host/CMakeLists.txt`

- [ ] **Step 2.1: Declare the full index API**

Create `core/include/rsvp/index.hpp` (the full API for Tasks 2–4; parse/at are stubbed until later tasks):
```cpp
#pragma once
#include "rsvp/token.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace rsvp {

struct DocMeta {
    std::string   title;
    std::string   author;
    std::uint32_t sourceSize  = 0;   // source file size in bytes (cache invalidation)
    std::uint32_t sourceMtime = 0;   // source file mtime (unix seconds)
};

struct Chapter {
    std::uint32_t wordOffset = 0;
    std::string   title;
};

// Default words-per-entry for the sparse seek table.
constexpr std::uint32_t kDefaultSeekInterval = 256;

// Serialize a tokenized document + metadata + chapters into the compiled-index byte format.
std::vector<std::uint8_t> serializeIndex(const Document& doc, const DocMeta& meta,
                                         const std::vector<Chapter>& chapters,
                                         std::uint32_t seekInterval = kDefaultSeekInterval);

// A parsed, queryable compiled index. Owns the decoded token stream + tables.
class CompiledIndex {
public:
    // Parse bytes. On bad magic/version/truncation, ok() is false.
    static CompiledIndex parse(const std::vector<std::uint8_t>& bytes);

    bool ok() const { return ok_; }
    const DocMeta& meta() const { return meta_; }
    const std::vector<Chapter>& chapters() const { return chapters_; }
    std::size_t wordCount() const { return wordCount_; }

    Document toDocument() const;                 // reconstruct all tokens
    Token    at(std::size_t wordIndex) const;    // O(seekInterval) random access; {} if out of range

private:
    bool                       ok_ = false;
    DocMeta                    meta_;
    std::vector<Chapter>       chapters_;
    std::size_t                wordCount_ = 0;
    std::uint32_t              seekInterval_ = kDefaultSeekInterval;
    std::vector<std::uint32_t> seekOffsets_;   // byte offsets into the token stream
    std::vector<std::uint8_t>  tokenStream_;   // the token-stream section
};

// Cheap cache-invalidation check: does the index's recorded source stats match?
// Reads only the fixed header; false on bad magic/version/short buffer.
bool indexMatchesSource(const std::vector<std::uint8_t>& bytes,
                        std::uint32_t sourceSize, std::uint32_t sourceMtime);

} // namespace rsvp
```

- [ ] **Step 2.2: Implement `serializeIndex` (and stub parse/at/indexMatchesSource)**

Create `core/src/index.cpp`:
```cpp
#include "rsvp/index.hpp"
#include "rsvp/byteio.hpp"

namespace rsvp {
using namespace byteio;

namespace {
const char     kMagic[4] = {'R', 'S', 'V', 'I'};
constexpr std::uint16_t kVersion = 1;
}

std::vector<std::uint8_t> serializeIndex(const Document& doc, const DocMeta& meta,
                                         const std::vector<Chapter>& chapters,
                                         std::uint32_t seekInterval) {
    if (seekInterval == 0) seekInterval = kDefaultSeekInterval;

    // Build the token stream first, recording seek offsets at each interval boundary.
    std::vector<std::uint8_t> ts;
    std::vector<std::uint32_t> seekOffsets;
    for (std::size_t i = 0; i < doc.tokens.size(); ++i) {
        if (i % seekInterval == 0) seekOffsets.push_back(static_cast<std::uint32_t>(ts.size()));
        const Token& t = doc.tokens[i];
        ts.push_back(t.flags);
        putU16(ts, static_cast<std::uint16_t>(t.text.size()));
        ts.insert(ts.end(), t.text.begin(), t.text.end());
    }

    std::vector<std::uint8_t> b;
    b.insert(b.end(), kMagic, kMagic + 4);
    putU16(b, kVersion);
    putU16(b, 0); // flags (reserved)
    putU32(b, meta.sourceSize);
    putU32(b, meta.sourceMtime);
    putU32(b, static_cast<std::uint32_t>(doc.tokens.size()));
    putU32(b, static_cast<std::uint32_t>(chapters.size()));
    putU32(b, seekInterval);
    putU16(b, static_cast<std::uint16_t>(meta.title.size()));
    b.insert(b.end(), meta.title.begin(), meta.title.end());
    putU16(b, static_cast<std::uint16_t>(meta.author.size()));
    b.insert(b.end(), meta.author.begin(), meta.author.end());
    for (const Chapter& c : chapters) {
        putU32(b, c.wordOffset);
        putU16(b, static_cast<std::uint16_t>(c.title.size()));
        b.insert(b.end(), c.title.begin(), c.title.end());
    }
    putU32(b, static_cast<std::uint32_t>(seekOffsets.size()));
    for (std::uint32_t off : seekOffsets) putU32(b, off);
    b.insert(b.end(), ts.begin(), ts.end());
    return b;
}

// --- stubs replaced in Tasks 3 and 4 ---
CompiledIndex CompiledIndex::parse(const std::vector<std::uint8_t>&) { return CompiledIndex{}; }
Document CompiledIndex::toDocument() const { return Document{}; }
Token    CompiledIndex::at(std::size_t) const { return Token{}; }
bool indexMatchesSource(const std::vector<std::uint8_t>&, std::uint32_t, std::uint32_t) { return false; }

} // namespace rsvp
```

- [ ] **Step 2.3: Write the failing test (header layout)**

Create `test/host/test_index.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/index.hpp"
#include "rsvp/byteio.hpp"
#include "rsvp/tokenize.hpp"
using namespace rsvp;

TEST_CASE("serializeIndex writes magic, version, and the header counts") {
    Document d = tokenizePlainText("The cat sat. It ran."); // 5 words
    DocMeta meta;
    meta.title = "T"; meta.author = "A";
    meta.sourceSize = 20; meta.sourceMtime = 12345;
    std::vector<std::uint8_t> b = serializeIndex(d, meta, /*chapters*/{}, /*seekInterval*/4);

    CHECK(b[0] == 'R'); CHECK(b[1] == 'S'); CHECK(b[2] == 'V'); CHECK(b[3] == 'I');
    std::size_t off = 4;
    CHECK(byteio::getU16(b, off) == 1);       // version
    CHECK(byteio::getU16(b, off) == 0);       // flags
    CHECK(byteio::getU32(b, off) == 20);      // sourceSize
    CHECK(byteio::getU32(b, off) == 12345);   // sourceMtime
    CHECK(byteio::getU32(b, off) == 5);       // wordCount
    CHECK(byteio::getU32(b, off) == 0);       // chapterCount
    CHECK(byteio::getU32(b, off) == 4);       // seekInterval
    CHECK(byteio::getU16(b, off) == 1);       // titleLen
    CHECK(b[off] == 'T'); off += 1;
    CHECK(byteio::getU16(b, off) == 1);       // authorLen
    CHECK(b[off] == 'A'); off += 1;
}
```

- [ ] **Step 2.4: Wire into CMake**

In `test/host/CMakeLists.txt`, replace the `add_executable(rsvp_tests ...)` block with:
```cmake
add_executable(rsvp_tests
    test_main.cpp
    test_token.cpp
    test_orp.cpp
    test_pacing.cpp
    test_player.cpp
    test_tokenize.cpp
    test_byteio.cpp
    test_index.cpp
    ${CORE_DIR}/src/orp.cpp
    ${CORE_DIR}/src/pacing.cpp
    ${CORE_DIR}/src/player.cpp
    ${CORE_DIR}/src/tokenize.cpp
    ${CORE_DIR}/src/index.cpp
)
```

- [ ] **Step 2.5: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed (cumulative **30 test cases**). This test pins the exact header layout; it passes once `serializeIndex` is implemented. Paste the summary lines.

- [ ] **Step 2.6: Commit**

```bash
git add core/include/rsvp/index.hpp core/src/index.cpp test/host/test_index.cpp test/host/CMakeLists.txt
git commit -m "feat(core): compiled-index serialization" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Parse + document round-trip

**Files:**
- Modify: `core/src/index.cpp` (replace the `parse` and `toDocument` stubs)
- Modify: `test/host/test_index.cpp` (add tests)

- [ ] **Step 3.1: Write the failing tests**

Append to `test/host/test_index.cpp`:
```cpp
TEST_CASE("serialize -> parse round-trips document, flags, and metadata") {
    Document d = tokenizePlainText("The cat sat. It ran.\n\nEnd here."); // 7 words
    DocMeta meta;
    meta.title = "Title"; meta.author = "Auth";
    meta.sourceSize = 99; meta.sourceMtime = 7;
    std::vector<Chapter> chapters = { {0, "Start"}, {3, "Next"} };

    std::vector<std::uint8_t> b = serializeIndex(d, meta, chapters, 4);
    CompiledIndex idx = CompiledIndex::parse(b);
    REQUIRE(idx.ok());

    CHECK(idx.meta().title == "Title");
    CHECK(idx.meta().author == "Auth");
    CHECK(idx.meta().sourceSize == 99);
    CHECK(idx.meta().sourceMtime == 7);
    CHECK(idx.wordCount() == d.size());
    REQUIRE(idx.chapters().size() == 2);
    CHECK(idx.chapters()[0].wordOffset == 0);
    CHECK(idx.chapters()[0].title == "Start");
    CHECK(idx.chapters()[1].wordOffset == 3);
    CHECK(idx.chapters()[1].title == "Next");

    Document back = idx.toDocument();
    REQUIRE(back.size() == d.size());
    for (std::size_t i = 0; i < d.size(); ++i) {
        CHECK(back.tokens[i].text == d.tokens[i].text);
        CHECK(back.tokens[i].flags == d.tokens[i].flags);
    }
}

TEST_CASE("parse rejects bad magic and truncated buffers") {
    std::vector<std::uint8_t> badMagic = {'X','X','X','X', 1,0};
    CHECK_FALSE(CompiledIndex::parse(badMagic).ok());
    std::vector<std::uint8_t> tooShort = {'R','S','V','I'};
    CHECK_FALSE(CompiledIndex::parse(tooShort).ok());
}
```

- [ ] **Step 3.2: Build + run → FAIL**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: FAILURE!` — `parse` is still the stub (`ok()` is false), so `REQUIRE(idx.ok())` fails. (The `parse rejects…` case may already pass since the stub returns not-ok, but the round-trip case fails.) Confirm FAILURE.

- [ ] **Step 3.3: Implement `parse` and `toDocument`**

In `core/src/index.cpp`, replace the two stub lines:
```cpp
CompiledIndex CompiledIndex::parse(const std::vector<std::uint8_t>&) { return CompiledIndex{}; }
Document CompiledIndex::toDocument() const { return Document{}; }
```
with:
```cpp
CompiledIndex CompiledIndex::parse(const std::vector<std::uint8_t>& b) {
    CompiledIndex idx;
    std::size_t off = 0;
    auto need = [&](std::size_t k) { return off + k <= b.size(); };

    if (!need(4) || b[0] != 'R' || b[1] != 'S' || b[2] != 'V' || b[3] != 'I') return idx;
    off = 4;
    if (!need(2)) return idx;
    if (getU16(b, off) != kVersion) return idx;
    if (!need(2)) return idx; getU16(b, off);                       // flags
    if (!need(4)) return idx; idx.meta_.sourceSize  = getU32(b, off);
    if (!need(4)) return idx; idx.meta_.sourceMtime = getU32(b, off);
    if (!need(4)) return idx; const std::uint32_t wc = getU32(b, off);
    if (!need(4)) return idx; const std::uint32_t cc = getU32(b, off);
    if (!need(4)) return idx; idx.seekInterval_ = getU32(b, off);
    if (idx.seekInterval_ == 0) return idx;

    if (!need(2)) return idx; const std::uint16_t tl = getU16(b, off);
    if (!need(tl)) return idx; idx.meta_.title.assign(b.begin() + off, b.begin() + off + tl); off += tl;
    if (!need(2)) return idx; const std::uint16_t al = getU16(b, off);
    if (!need(al)) return idx; idx.meta_.author.assign(b.begin() + off, b.begin() + off + al); off += al;

    for (std::uint32_t i = 0; i < cc; ++i) {
        if (!need(4)) return idx; const std::uint32_t wo = getU32(b, off);
        if (!need(2)) return idx; const std::uint16_t cl = getU16(b, off);
        if (!need(cl)) return idx; std::string ct(b.begin() + off, b.begin() + off + cl); off += cl;
        idx.chapters_.push_back(Chapter{wo, ct});
    }

    if (!need(4)) return idx; const std::uint32_t sc = getU32(b, off);
    for (std::uint32_t i = 0; i < sc; ++i) {
        if (!need(4)) return idx;
        idx.seekOffsets_.push_back(getU32(b, off));
    }

    idx.tokenStream_.assign(b.begin() + off, b.end());
    idx.wordCount_ = wc;
    idx.ok_ = true;
    return idx;
}

Document CompiledIndex::toDocument() const {
    Document d;
    if (!ok_) return d;
    std::size_t off = 0;
    for (std::size_t i = 0; i < wordCount_; ++i) {
        if (off + 3 > tokenStream_.size()) break;            // flags(1) + len(2)
        const std::uint8_t flags = tokenStream_[off++];
        const std::uint16_t len = getU16(tokenStream_, off);
        if (off + len > tokenStream_.size()) break;
        std::string w(tokenStream_.begin() + off, tokenStream_.begin() + off + len);
        off += len;
        d.tokens.push_back(Token{w, flags});
    }
    return d;
}
```

- [ ] **Step 3.4: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed (cumulative **32 test cases**). Paste the summary lines.

- [ ] **Step 3.5: Commit**

```bash
git add core/src/index.cpp test/host/test_index.cpp
git commit -m "feat(core): compiled-index parse + document round-trip" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Sparse random access + cache invalidation

**Files:**
- Modify: `core/src/index.cpp` (replace the `at` and `indexMatchesSource` stubs)
- Modify: `test/host/test_index.cpp` (add tests)

- [ ] **Step 4.1: Write the failing tests**

Append to `test/host/test_index.cpp`:
```cpp
TEST_CASE("CompiledIndex::at random-accesses via the sparse seek table") {
    Document d = tokenizePlainText("a b c d e f g h"); // 8 words
    REQUIRE(d.size() == 8);
    DocMeta meta;
    std::vector<std::uint8_t> b = serializeIndex(d, meta, {}, /*seekInterval*/3); // entries at 0,3,6
    CompiledIndex idx = CompiledIndex::parse(b);
    REQUIRE(idx.ok());

    CHECK(idx.at(0).text == "a");
    CHECK(idx.at(2).text == "c");
    CHECK(idx.at(3).text == "d");   // exactly on a seek entry
    CHECK(idx.at(5).text == "f");
    CHECK(idx.at(6).text == "g");   // on a seek entry
    CHECK(idx.at(7).text == "h");
    CHECK(idx.at(8).text == "");    // out of range -> empty token

    Document all = idx.toDocument();
    for (std::size_t i = 0; i < idx.wordCount(); ++i)
        CHECK(idx.at(i).text == all.tokens[i].text);
}

TEST_CASE("indexMatchesSource compares the recorded source stats") {
    Document d = tokenizePlainText("hello world");
    DocMeta meta; meta.sourceSize = 11; meta.sourceMtime = 42;
    std::vector<std::uint8_t> b = serializeIndex(d, meta, {});
    CHECK(indexMatchesSource(b, 11, 42));
    CHECK_FALSE(indexMatchesSource(b, 12, 42));   // size differs
    CHECK_FALSE(indexMatchesSource(b, 11, 43));   // mtime differs
    std::vector<std::uint8_t> bad = {'X','X','X','X'};
    CHECK_FALSE(indexMatchesSource(bad, 11, 42)); // bad magic
}
```

- [ ] **Step 4.2: Build + run → FAIL**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: FAILURE!` — `at` and `indexMatchesSource` are still stubs (`at` returns `{}`, `indexMatchesSource` returns false), so the new checks fail. Confirm FAILURE.

- [ ] **Step 4.3: Implement `at` and `indexMatchesSource`**

In `core/src/index.cpp`, replace the two stub lines:
```cpp
Token    CompiledIndex::at(std::size_t) const { return Token{}; }
bool indexMatchesSource(const std::vector<std::uint8_t>&, std::uint32_t, std::uint32_t) { return false; }
```
with:
```cpp
Token CompiledIndex::at(std::size_t wordIndex) const {
    if (!ok_ || wordIndex >= wordCount_) return Token{};
    const std::size_t entry = wordIndex / seekInterval_;
    std::size_t off = (entry < seekOffsets_.size()) ? seekOffsets_[entry] : 0;
    const std::size_t toSkip = wordIndex - entry * seekInterval_;
    for (std::size_t s = 0; s < toSkip; ++s) {
        if (off + 3 > tokenStream_.size()) return Token{};
        off += 1;                                   // flags
        const std::uint16_t len = getU16(tokenStream_, off);
        off += len;
    }
    if (off + 3 > tokenStream_.size()) return Token{};
    const std::uint8_t flags = tokenStream_[off++];
    const std::uint16_t len = getU16(tokenStream_, off);
    if (off + len > tokenStream_.size()) return Token{};
    return Token{ std::string(tokenStream_.begin() + off, tokenStream_.begin() + off + len), flags };
}

bool indexMatchesSource(const std::vector<std::uint8_t>& b,
                        std::uint32_t sourceSize, std::uint32_t sourceMtime) {
    if (b.size() < 16) return false;                // magic(4)+ver(2)+flags(2)+size(4)+mtime(4)
    if (b[0] != 'R' || b[1] != 'S' || b[2] != 'V' || b[3] != 'I') return false;
    std::size_t off = 4;
    if (getU16(b, off) != kVersion) return false;   // version
    getU16(b, off);                                 // flags
    const std::uint32_t recSize  = getU32(b, off);
    const std::uint32_t recMtime = getU32(b, off);
    return recSize == sourceSize && recMtime == sourceMtime;
}
```

- [ ] **Step 4.4: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed (cumulative **34 test cases**). Paste the summary lines.

- [ ] **Step 4.5: Commit**

```bash
git add core/src/index.cpp test/host/test_index.cpp
git commit -m "feat(core): sparse random access + index cache invalidation" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-review (performed against the spec)

**1. Spec coverage (this plan's slice of design spec §4–6):**
- Uniform compiled token stream for plain text (§4 seam, §6 token records `[flags][len][utf8]`) → Tasks 0 (tokenizer) + 2 (serialize). ✓
- Header with magic/version, source size+mtime, title/author, word/chapter counts (§6) → Task 2 + Task 3 (parse). ✓
- Chapter table (§6) → Task 2/3 (serialized + parsed; tokenizer leaves it empty for TXT per decision #4). ✓
- Sparse seek table for fast resume/jump (§6) → Task 2 (write) + Task 4 (`at`). ✓
- Cache invalidation on size+mtime (§5, §11) → Task 4 (`indexMatchesSource`). ✓
- Hardware-free, host-tested (§4, §12) → entire plan. ✓
- *Deferred (named in Scope):* SD/FATFS I/O, `/.rsvp/` on-disk cache, cover thumbnails, EPUB indexer, title-from-filename — correctly out of this plan.

**2. Placeholder scan:** No "TBD/TODO/handle X appropriately". Every code step is complete; stubs are explicit and replaced in the named later step. ✓

**3. Type consistency:** `tokenizePlainText`, `DocMeta{title,author,sourceSize,sourceMtime}`, `Chapter{wordOffset,title}`, `serializeIndex(doc,meta,chapters,seekInterval)`, `CompiledIndex::{parse,ok,meta,chapters,wordCount,toDocument,at}`, `indexMatchesSource`, `byteio::{putU16,putU32,getU16,getU32}`, `kDefaultSeekInterval`, `kMagic`/`kVersion` are used identically across headers, sources, and tests. Token-record layout (`flags` u8, `len` u16, bytes) is written in Task 2 and read identically in Tasks 3–4. ✓

---

## Execution handoff

Implement task-by-task with `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans`, branching `feature/rsvp-doc-pipeline` off `feature/rsvp-engine-core`. After Task 4, plain text flows end-to-end: `tokenizePlainText` → `serializeIndex` → `CompiledIndex` (random-access + invalidation), ready for the ESP-IDF file layer to persist into `/.rsvp/` and feed the `Player`.

# EPUB Assembly (ZIP → Compiled Index) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn EPUB (ZIP) bytes into a Plan-2 compiled index end-to-end: unzip → `container.xml` → OPF → each spine XHTML through `htmlToText` → tokenize → serialize, with one chapter per spine document.

**Architecture:** Two small units on top of the already-vendored miniz: a `zipreader` (extract a named entry from an in-memory ZIP via miniz) and an `epub` module (`resolveHref` to resolve spine paths against the OPF directory, and `epubToIndex` tying the whole pipeline together). Pure logic + miniz; host-tested by building EPUBs in-memory with miniz's writer. No hardware/SD (the device file layer calls `epubToIndex` with bytes read from the card). Strict TDD.

**Tech Stack:** C++17, CMake with the Visual Studio 2022 (MSVC) generator, doctest v2.4.11, vendored miniz 3.0.2. Reuses `parseContainerOpfPath`/`parseOpf` (Plan 4), `htmlToText` (Plan 3), `tokenizePlainText` (Plan 2), `serializeIndex`/`CompiledIndex` (Plan 2).

---

## Scope & rationale

This completes the spec's EPUB indexer (§4–6): the prior plans built every pure-logic piece; this plan wires them to a real ZIP. miniz is already vendored under `third_party/miniz` and compiling in the build (commit `97e9727`). EPUBs are built in-memory with miniz's writer for host tests — no binary fixtures needed.

**In scope:** `readZipEntry` (miniz wrapper); `resolveHref`; `epubToIndex` (container → OPF → spine → htmlToText → tokenize → serialize, chapter per spine doc).
**Out of scope (later):** chapter titles from NCX/nav, cover-image extraction/thumbnailing, the SD/FATFS file layer, hardware/LVGL.

**Branch:** Already on **`feature/rsvp-epub-assembly`** (miniz vendored here). Continue on it.

## Design decisions (please review before execution)

1. **`readZipEntry` interface:** `bool readZipEntry(const std::vector<std::uint8_t>& zip, const std::string& name, std::string& out)` — returns the raw entry bytes; false on a bad ZIP or missing entry. Memory-based (the device reads the EPUB file into a buffer; miniz also has a file API the SD layer can swap to later).
2. **Chapters:** one chapter per spine document, recorded in the index's chapter table at its starting word offset. The first token of every spine doc **after the first** gets `FLAG_CHAPTER_START` (so the pacing engine pauses at chapter boundaries). Chapter **titles are left empty** for now (NCX/nav titles are a later enhancement).
3. **Href resolution:** spine hrefs resolve against the OPF file's directory, normalizing `./` and `../` (e.g. OPF at `OEBPS/content.opf`, href `chap1.xhtml` → `OEBPS/chap1.xhtml`).
4. **Failure = empty result:** `epubToIndex` returns an empty vector if the bytes aren't a ZIP, or there's no `container.xml`/OPF, or no spine document yields any text. No exceptions. A spine entry that's missing or empty is skipped (best-effort).
5. **Metadata:** title/author from the OPF; `sourceSize`/`sourceMtime` passed in by the caller (the file layer that read the EPUB).

## File structure

```
core/include/rsvp/zipreader.hpp  — readZipEntry
core/src/zipreader.cpp           — miniz-backed impl
core/include/rsvp/epub.hpp       — resolveHref, epubToIndex
core/src/epub.cpp                — impls
test/host/test_zipreader.cpp     — zip reader tests (build zips with miniz writer)
test/host/test_epub.cpp          — resolveHref + end-to-end epubToIndex tests
test/host/CMakeLists.txt         — MODIFIED: add the 2 sources + 2 tests
```

## Conventions

- **TDD:** failing test → watch it fail → minimal impl → watch it pass → commit.
- **Toolchain:** `build/host/` is configured (VS 2022, C enabled, miniz compiled). Build `cmake --build build/host`; run `./build/host/Debug/rsvp_tests.exe`.
- **Commit trailer:** end every commit with `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
- **Branch:** `feature/rsvp-epub-assembly`. Do not implement on `main`.
- **Counts:** suite starts at **60** cases (incl. the miniz round-trip). Key signal is **`Status: SUCCESS!` with 0 failed**.

---

### Task 0: `readZipEntry` — extract a ZIP entry via miniz

**Files:**
- Create: `core/include/rsvp/zipreader.hpp`
- Create: `core/src/zipreader.cpp`
- Create: `test/host/test_zipreader.cpp`
- Modify: `test/host/CMakeLists.txt`

- [ ] **Step 0.1: Declare the API + a failing stub**

Create `core/include/rsvp/zipreader.hpp`:
```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rsvp {

// Extract entry `name` from an in-memory ZIP archive into `out` (raw bytes).
// Returns false if `zip` is not a valid ZIP or the entry is missing. `out` is
// always cleared first.
bool readZipEntry(const std::vector<std::uint8_t>& zip, const std::string& name, std::string& out);

} // namespace rsvp
```

Create `core/src/zipreader.cpp` as a STUB:
```cpp
#include "rsvp/zipreader.hpp"

namespace rsvp {

bool readZipEntry(const std::vector<std::uint8_t>&, const std::string&, std::string& out) {
    out.clear();
    return false; // STUB
}

} // namespace rsvp
```

- [ ] **Step 0.2: Write the failing tests**

Create `test/host/test_zipreader.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/zipreader.hpp"
#include "miniz.h"
#include <string>
#include <utility>
#include <vector>
using namespace rsvp;

// Build an in-memory ZIP from {name, content} entries (miniz writer).
static std::vector<std::uint8_t> makeZip(std::initializer_list<std::pair<std::string, std::string>> entries) {
    mz_zip_archive z;
    mz_zip_zero_struct(&z);
    mz_zip_writer_init_heap(&z, 0, 0);
    for (const auto& e : entries)
        mz_zip_writer_add_mem(&z, e.first.c_str(), e.second.data(), e.second.size(), MZ_BEST_COMPRESSION);
    void* buf = nullptr;
    size_t sz = 0;
    mz_zip_writer_finalize_heap_archive(&z, &buf, &sz);
    std::vector<std::uint8_t> out(static_cast<std::uint8_t*>(buf), static_cast<std::uint8_t*>(buf) + sz);
    mz_zip_writer_end(&z);
    mz_free(buf);
    return out;
}

TEST_CASE("readZipEntry: reads named entries from an in-memory ZIP") {
    auto zip = makeZip({{"a.txt", "alpha"}, {"dir/b.xml", "<x>beta</x>"}});
    std::string out;
    REQUIRE(readZipEntry(zip, "a.txt", out));
    CHECK(out == "alpha");
    REQUIRE(readZipEntry(zip, "dir/b.xml", out));
    CHECK(out == "<x>beta</x>");
}

TEST_CASE("readZipEntry: missing entry or invalid zip returns false and clears out") {
    auto zip = makeZip({{"a.txt", "alpha"}});
    std::string out = "untouched";
    CHECK_FALSE(readZipEntry(zip, "missing.txt", out));
    CHECK(out == "");
    CHECK_FALSE(readZipEntry(std::vector<std::uint8_t>{1, 2, 3, 4}, "a.txt", out)); // not a zip
}
```

- [ ] **Step 0.3: Wire into CMake**

In `test/host/CMakeLists.txt`, add `test_zipreader.cpp` (to the test list, after `test_miniz.cpp`) and `${CORE_DIR}/src/zipreader.cpp` (to the sources, after `${CORE_DIR}/src/opf.cpp`). The relevant parts become:
```cmake
    test_opf.cpp
    test_miniz.cpp
    test_zipreader.cpp
    ${CORE_DIR}/src/orp.cpp
```
and:
```cmake
    ${CORE_DIR}/src/opf.cpp
    ${CORE_DIR}/src/zipreader.cpp
    ${MINIZ_DIR}/miniz.c
)
```

- [ ] **Step 0.4: Build + run → FAIL**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: FAILURE!` — the stub returns false, so the `REQUIRE(readZipEntry(...))` calls fail. Confirm FAILURE.

- [ ] **Step 0.5: Implement `readZipEntry` (replace the body of `core/src/zipreader.cpp`)**
```cpp
#include "rsvp/zipreader.hpp"
#include "miniz.h"

namespace rsvp {

bool readZipEntry(const std::vector<std::uint8_t>& zip, const std::string& name, std::string& out) {
    out.clear();
    if (zip.empty()) return false;
    mz_zip_archive za;
    mz_zip_zero_struct(&za);
    if (!mz_zip_reader_init_mem(&za, zip.data(), zip.size(), 0)) return false;
    bool ok = false;
    const int idx = mz_zip_reader_locate_file(&za, name.c_str(), nullptr, 0);
    if (idx >= 0) {
        std::size_t sz = 0;
        void* p = mz_zip_reader_extract_to_heap(&za, static_cast<mz_uint>(idx), &sz, 0);
        if (p) {
            out.assign(static_cast<const char*>(p), sz);
            mz_free(p);
            ok = true;
        }
    }
    mz_zip_reader_end(&za);
    return ok;
}

} // namespace rsvp
```

- [ ] **Step 0.6: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed, **62 test cases** (60 + 2). Paste the summary lines.

- [ ] **Step 0.7: Commit**

```bash
git add core/include/rsvp/zipreader.hpp core/src/zipreader.cpp test/host/test_zipreader.cpp test/host/CMakeLists.txt
git commit -m "feat(core): in-memory ZIP entry reader (miniz)" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 1: `resolveHref` — resolve spine paths against the OPF directory

**Files:**
- Create: `core/include/rsvp/epub.hpp`
- Create: `core/src/epub.cpp`
- Create: `test/host/test_epub.cpp`
- Modify: `test/host/CMakeLists.txt`

- [ ] **Step 1.1: Declare the API + stub `epubToIndex`**

Create `core/include/rsvp/epub.hpp`:
```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rsvp {

// Resolve a content href against the OPF file's ZIP-internal path, normalizing
// "./" and "../". e.g. resolveHref("OEBPS/content.opf", "chap1.xhtml") == "OEBPS/chap1.xhtml".
std::string resolveHref(const std::string& opfPath, const std::string& href);

// Build a compiled index (Plan 2 format) from EPUB (ZIP) bytes. One chapter per spine
// document (2nd+ chapters' first token carries FLAG_CHAPTER_START). Returns empty on
// failure (not a ZIP / no container / no OPF / no readable spine text).
std::vector<std::uint8_t> epubToIndex(const std::vector<std::uint8_t>& epub,
                                      std::uint32_t sourceSize, std::uint32_t sourceMtime);

} // namespace rsvp
```

Create `core/src/epub.cpp` with `resolveHref` implemented and `epubToIndex` STUBBED:
```cpp
#include "rsvp/epub.hpp"

namespace rsvp {

std::string resolveHref(const std::string& opfPath, const std::string& href) {
    const std::size_t slash = opfPath.find_last_of('/');
    const std::string base = (slash == std::string::npos) ? std::string() : opfPath.substr(0, slash + 1);
    const std::string path = base + href;

    std::vector<std::string> parts;
    std::string seg;
    auto flush = [&]() {
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!seg.empty() && seg != ".") parts.push_back(seg);
        seg.clear();
    };
    for (const char c : path) {
        if (c == '/') flush();
        else seg.push_back(c);
    }
    flush();

    std::string out;
    for (std::size_t k = 0; k < parts.size(); ++k) {
        if (k) out.push_back('/');
        out += parts[k];
    }
    return out;
}

// --- stub replaced in Task 2 ---
std::vector<std::uint8_t> epubToIndex(const std::vector<std::uint8_t>&, std::uint32_t, std::uint32_t) {
    return {};
}

} // namespace rsvp
```
(`epub.hpp` includes `<vector>`/`<string>`, so `std::vector`/`std::string` are available in `epub.cpp`.)

- [ ] **Step 1.2: Write the failing tests**

Create `test/host/test_epub.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/epub.hpp"
using namespace rsvp;

TEST_CASE("resolveHref: resolves spine hrefs against the OPF directory") {
    CHECK(resolveHref("OEBPS/content.opf", "chap1.xhtml") == "OEBPS/chap1.xhtml");
    CHECK(resolveHref("content.opf", "a.xhtml") == "a.xhtml");
    CHECK(resolveHref("OEBPS/text/content.opf", "../img/c.png") == "OEBPS/img/c.png");
    CHECK(resolveHref("OEBPS/content.opf", "sub/d.xhtml") == "OEBPS/sub/d.xhtml");
    CHECK(resolveHref("OEBPS/content.opf", "./x.xhtml") == "OEBPS/x.xhtml");
}
```

- [ ] **Step 1.3: Wire into CMake**

In `test/host/CMakeLists.txt`, add `test_epub.cpp` (after `test_zipreader.cpp`) and `${CORE_DIR}/src/epub.cpp` (after `${CORE_DIR}/src/zipreader.cpp`):
```cmake
    test_zipreader.cpp
    test_epub.cpp
    ${CORE_DIR}/src/orp.cpp
```
and:
```cmake
    ${CORE_DIR}/src/zipreader.cpp
    ${CORE_DIR}/src/epub.cpp
    ${MINIZ_DIR}/miniz.c
)
```

- [ ] **Step 1.4: Build + run → expect mostly PASS (resolveHref is implemented)**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed, **63 test cases** (62 + 1). `resolveHref` is implemented directly (pure logic), so its test passes immediately; `epubToIndex` is an unexercised stub. Paste the summary lines. (If a `resolveHref` case fails, fix the normalization to match the expected paths before continuing.)

- [ ] **Step 1.5: Commit**

```bash
git add core/include/rsvp/epub.hpp core/src/epub.cpp test/host/test_epub.cpp test/host/CMakeLists.txt
git commit -m "feat(core): resolve EPUB spine hrefs against the OPF directory" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: `epubToIndex` — end-to-end EPUB → compiled index

**Files:**
- Modify: `core/src/epub.cpp` (add includes + replace the `epubToIndex` stub)
- Modify: `test/host/test_epub.cpp` (add the end-to-end tests)

- [ ] **Step 2.1: Write the failing tests**

Append to `test/host/test_epub.cpp` (note the new includes at the very top of the file — add them after the existing `#include "rsvp/epub.hpp"`):
```cpp
#include "rsvp/index.hpp"
#include "miniz.h"
#include <string>
#include <utility>
#include <vector>

// Build an in-memory ZIP (EPUB) from {name, content} entries.
static std::vector<std::uint8_t> makeEpub(std::initializer_list<std::pair<std::string, std::string>> entries) {
    mz_zip_archive z;
    mz_zip_zero_struct(&z);
    mz_zip_writer_init_heap(&z, 0, 0);
    for (const auto& e : entries)
        mz_zip_writer_add_mem(&z, e.first.c_str(), e.second.data(), e.second.size(), MZ_BEST_COMPRESSION);
    void* buf = nullptr;
    size_t sz = 0;
    mz_zip_writer_finalize_heap_archive(&z, &buf, &sz);
    std::vector<std::uint8_t> out(static_cast<std::uint8_t*>(buf), static_cast<std::uint8_t*>(buf) + sz);
    mz_zip_writer_end(&z);
    mz_free(buf);
    return out;
}

TEST_CASE("epubToIndex: end-to-end EPUB -> compiled index with chapters") {
    const std::string container =
        "<?xml version='1.0'?><container><rootfiles>"
        "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
        "</rootfiles></container>";
    const std::string opf =
        "<package><metadata><dc:title>My Book</dc:title><dc:creator>An Author</dc:creator></metadata>"
        "<manifest>"
        "<item id=\"c1\" href=\"c1.xhtml\" media-type=\"application/xhtml+xml\"/>"
        "<item id=\"c2\" href=\"c2.xhtml\" media-type=\"application/xhtml+xml\"/>"
        "</manifest>"
        "<spine><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>";
    const std::string c1 = "<html><body><p>The cat sat.</p></body></html>";
    const std::string c2 = "<html><body><p>It ran fast.</p></body></html>";

    auto epub = makeEpub({
        {"mimetype", "application/epub+zip"},
        {"META-INF/container.xml", container},
        {"OEBPS/content.opf", opf},
        {"OEBPS/c1.xhtml", c1},
        {"OEBPS/c2.xhtml", c2},
    });

    std::vector<std::uint8_t> bytes = epubToIndex(epub, 1234, 5678);
    REQUIRE(!bytes.empty());

    CompiledIndex idx = CompiledIndex::parse(bytes);
    REQUIRE(idx.ok());
    CHECK(idx.meta().title == "My Book");
    CHECK(idx.meta().author == "An Author");
    CHECK(idx.meta().sourceSize == 1234);
    CHECK(idx.meta().sourceMtime == 5678);

    Document d = idx.toDocument();
    REQUIRE(d.size() == 6); // The cat sat. | It ran fast.
    CHECK(d.tokens[0].text == "The");
    CHECK(d.tokens[3].text == "It");
    CHECK(d.tokens[3].has(FLAG_CHAPTER_START));        // first token of the 2nd spine doc
    CHECK_FALSE(d.tokens[0].has(FLAG_CHAPTER_START));  // not the very first token

    REQUIRE(idx.chapters().size() == 2);
    CHECK(idx.chapters()[0].wordOffset == 0);
    CHECK(idx.chapters()[1].wordOffset == 3);
}

TEST_CASE("epubToIndex: invalid bytes / no container -> empty") {
    CHECK(epubToIndex(std::vector<std::uint8_t>{1, 2, 3}, 0, 0).empty());
    auto noContainer = makeEpub({{"random.txt", "hello"}});
    CHECK(epubToIndex(noContainer, 0, 0).empty());
}
```

- [ ] **Step 2.2: Build + run → FAIL**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: FAILURE!` — `epubToIndex` is the stub (returns empty), so `REQUIRE(!bytes.empty())` fails. (The invalid-bytes case passes against the stub.) Confirm FAILURE.

- [ ] **Step 2.3: Implement `epubToIndex`**

In `core/src/epub.cpp`, add these includes after the existing `#include "rsvp/epub.hpp"`:
```cpp
#include "rsvp/epub.hpp"
#include "rsvp/zipreader.hpp"
#include "rsvp/opf.hpp"
#include "rsvp/htmltext.hpp"
#include "rsvp/tokenize.hpp"
#include "rsvp/index.hpp"
```
Then replace the stub:
```cpp
std::vector<std::uint8_t> epubToIndex(const std::vector<std::uint8_t>&, std::uint32_t, std::uint32_t) {
    return {};
}
```
with:
```cpp
std::vector<std::uint8_t> epubToIndex(const std::vector<std::uint8_t>& epub,
                                      std::uint32_t sourceSize, std::uint32_t sourceMtime) {
    std::string container;
    if (!readZipEntry(epub, "META-INF/container.xml", container)) return {};
    const std::string opfPath = parseContainerOpfPath(container);
    if (opfPath.empty()) return {};
    std::string opfXml;
    if (!readZipEntry(epub, opfPath, opfXml)) return {};
    const OpfData opf = parseOpf(opfXml);

    Document doc;
    std::vector<Chapter> chapters;
    for (const std::string& href : opf.spineHrefs) {
        std::string xhtml;
        if (!readZipEntry(epub, resolveHref(opfPath, href), xhtml)) continue;
        Document part = tokenizePlainText(htmlToText(xhtml));
        if (part.tokens.empty()) continue;
        if (!doc.tokens.empty()) part.tokens.front().flags |= FLAG_CHAPTER_START;
        chapters.push_back(Chapter{static_cast<std::uint32_t>(doc.tokens.size()), std::string()});
        for (Token& t : part.tokens) doc.tokens.push_back(std::move(t));
    }
    if (doc.tokens.empty()) return {};

    DocMeta meta;
    meta.title       = opf.title;
    meta.author      = opf.author;
    meta.sourceSize  = sourceSize;
    meta.sourceMtime = sourceMtime;
    return serializeIndex(doc, meta, chapters);
}
```

- [ ] **Step 2.4: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed, **65 test cases** (63 + 2). Paste the summary lines.

- [ ] **Step 2.5: Commit**

```bash
git add core/src/epub.cpp test/host/test_epub.cpp
git commit -m "feat(core): end-to-end EPUB-to-compiled-index assembly" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-review (performed against the spec)

**1. Spec coverage (EPUB indexer, §4–6):**
- Unzip EPUB entries → Task 0 (`readZipEntry` via miniz). ✓
- container.xml → OPF → spine (Plan 4) wired in → Task 2. ✓
- Spine XHTML → text (Plan 3 `htmlToText`) → tokens (Plan 2) → Task 2. ✓
- Chapter table (one per spine doc) + `FLAG_CHAPTER_START` → Task 2. ✓
- Compiled index with metadata + cache stats (Plan 2 `serializeIndex`) → Task 2. ✓
- Href resolution against the OPF dir → Task 1. ✓
- *Deferred (named in Scope):* chapter titles (NCX/nav), cover extraction, SD file layer, hardware.

**2. Placeholder scan:** No "TBD/TODO". Every code step complete; stubs explicit and replaced in the named step.

**3. Type consistency:** `readZipEntry(const std::vector<std::uint8_t>&, const std::string&, std::string&)`, `resolveHref(const std::string&, const std::string&)`, `epubToIndex(const std::vector<std::uint8_t>&, std::uint32_t, std::uint32_t)`, and reuse of `parseContainerOpfPath`/`parseOpf`/`OpfData`/`htmlToText`/`tokenizePlainText`/`Document`/`Token`/`Chapter`/`DocMeta`/`serializeIndex`/`CompiledIndex`/`FLAG_CHAPTER_START` match their definitions in Plans 2–4. ✓

---

## Execution handoff

Implement task-by-task with `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` on `feature/rsvp-epub-assembly`. After Task 2, `epubToIndex(bytes)` produces a complete, player-ready compiled index from a real EPUB — the entire content pipeline (TXT **and** EPUB) is done in host-tested logic, leaving only the ESP-IDF/HAL/LVGL device layer and the SD file glue.

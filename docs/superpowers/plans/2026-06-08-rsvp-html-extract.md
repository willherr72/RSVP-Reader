# XHTML → Text Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert XHTML/HTML into clean, paragraph-separated reading text (tags stripped, entities decoded, script/style dropped) that feeds the existing `tokenizePlainText` — the pure-logic heart of EPUB support.

**Architecture:** Two small units added to `core/`: a UTF-8 code-point encoder + an HTML entity decoder, and an `htmlToText` extractor built on them. Output is normalized text (`\n\n` between blocks, single spaces between words) → straight into `tokenizePlainText` → `Document`. No ZIP, no XML/OPF, no hardware — those are later plans. Same host harness (doctest, MSVC/VS 2022). Strict TDD.

**Tech Stack:** C++17, CMake with the Visual Studio 2022 (MSVC) generator, doctest v2.4.11. Builds on `rsvp::utf8`, `rsvp::Document`, `rsvp::tokenizePlainText`.

---

## Scope & rationale

Per the design spec's EPUB indexer (§4, the "XHTML → text" step), each spine document must be reduced to reading text before tokenization. This plan implements exactly that step, host-tested, reusing the Plan 2 tokenizer downstream. It is filesystem- and library-free, so it unit-tests on a host and compiles unchanged into the firmware.

**In scope:** UTF-8 code-point encoding; HTML entity decoding; `htmlToText` (tag stripping, block→paragraph, script/style removal, whitespace normalization); an end-to-end test through the tokenizer.
**Out of scope (later plans):** ZIP/`miniz` unzip, OPF/`container.xml` parsing, spine assembly, chapter extraction from headings, the EPUB-file indexer, and all hardware/SD work.

**Dependency / branch:** Builds on Plan 2 (`tokenizePlainText`), which lives on `feature/rsvp-doc-pipeline`. Execution branches **`feature/rsvp-html-extract` off `feature/rsvp-doc-pipeline`**.

## Design decisions (please review before execution)

1. **Output shape:** `htmlToText` returns plain text with **blocks separated by `\n\n`** and **words by single spaces** — exactly what `tokenizePlainText` expects (it turns `\n\n` into a paragraph break). The two compose; `htmlToText` does no tokenization itself.
2. **Block vs inline:** block elements (`p, div, h1–h6, li, ul, ol, blockquote, section, article, header, footer, figure, pre, hr, table, tr, td, th, body, main, aside, nav, dd, dt, dl, …`) insert a paragraph boundary on open **and** close. Inline tags (`b, i, em, strong, span, a, …`) are simply removed. `<br>` is a **soft space** (a word separator, not a paragraph) so `<br>`-heavy content doesn't pause every line.
3. **`<script>` / `<style>`:** their **content is dropped** entirely (skip to the matching close tag, case-insensitive).
4. **Whitespace:** all source whitespace (incl. literal newlines inside a block) collapses to a **single space** (standard HTML behavior); only block boundaries produce paragraph breaks.
5. **Entities:** decode `&amp; &lt; &gt; &quot; &apos;`, numeric `&#NN;` / `&#xHH;`, and common book punctuation (`&mdash; &ndash; &hellip; &lsquo; &rsquo; &ldquo; &rdquo; &copy; &reg; &trade; &deg;`). **`&nbsp;` → a regular space** (so words tokenize). Unknown/malformed entities are left **literal**. Attributes are ignored (we only need text). No exceptions — malformed input degrades gracefully.

## File structure

```
core/include/rsvp/utf8.hpp      — MODIFIED: add utf8::appendCodePoint (encode a code point to UTF-8)
core/include/rsvp/entity.hpp    — decodeEntities API
core/src/entity.cpp             — entity decoder impl
core/include/rsvp/htmltext.hpp  — htmlToText API
core/src/htmltext.cpp           — htmlToText impl (tag strip + block logic + normalize)
test/host/test_utf8.cpp         — appendCodePoint tests
test/host/test_entity.cpp       — entity tests
test/host/test_htmltext.cpp     — htmlToText tests + end-to-end-through-tokenizer test
test/host/CMakeLists.txt        — MODIFIED: add the new sources + tests
```

## Conventions

- **TDD:** failing test → watch it fail → minimal impl → watch it pass → commit.
- **Toolchain:** `build/host/` is already configured with `-G "Visual Studio 17 2022"`. Build `cmake --build build/host` (Debug; auto-reconfigures on CMakeLists changes); run `./build/host/Debug/rsvp_tests.exe`.
- **Commit trailer:** end every commit message with `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
- **Branch:** `feature/rsvp-html-extract` off `feature/rsvp-doc-pipeline` (created at execution time). Do not implement on `main`.
- **Counts:** the suite starts at **38** cases. "Expected" counts below assume that baseline; the key signal is **`Status: SUCCESS!` with 0 failed**.

---

### Task 0: UTF-8 code-point encoder

**Files:**
- Modify: `core/include/rsvp/utf8.hpp`
- Create: `test/host/test_utf8.cpp`
- Modify: `test/host/CMakeLists.txt`

- [ ] **Step 0.1: Add `appendCodePoint` to `core/include/rsvp/utf8.hpp`**

Inside `namespace rsvp { namespace utf8 {` (after the existing `byteOffset` function, before the closing `}}`), add:
```cpp
// Encode a Unicode code point as UTF-8 and append it to out. Code points above
// U+10FFFF (invalid) are skipped.
inline void appendCodePoint(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}
```
Note: `utf8.hpp` already includes `<cstdint>`, `<cstddef>`, `<string>` — no new includes needed (`std::uint32_t`, `std::string` are available).

- [ ] **Step 0.2: Create `test/host/test_utf8.cpp`**
```cpp
#include "doctest.h"
#include "rsvp/utf8.hpp"
using namespace rsvp;

TEST_CASE("utf8::appendCodePoint encodes 1-4 byte sequences") {
    std::string s;
    utf8::appendCodePoint(s, 0x41);     CHECK(s == "A");                  // 1 byte
    s.clear(); utf8::appendCodePoint(s, 0xE9);     CHECK(s == "\xC3\xA9");          // é, 2 bytes
    s.clear(); utf8::appendCodePoint(s, 0x2014);   CHECK(s == "\xE2\x80\x94");      // em dash, 3 bytes
    s.clear(); utf8::appendCodePoint(s, 0x1F600);  CHECK(s == "\xF0\x9F\x98\x80");  // emoji, 4 bytes
}

TEST_CASE("utf8::appendCodePoint output round-trips with utf8::length") {
    std::string s;
    utf8::appendCodePoint(s, 0x41);
    utf8::appendCodePoint(s, 0xE9);
    utf8::appendCodePoint(s, 0x2014);
    CHECK(utf8::length(s) == 3); // three code points regardless of byte count
}
```

- [ ] **Step 0.3: Wire into CMake**

In `test/host/CMakeLists.txt`, add `test_utf8.cpp` to the `add_executable(rsvp_tests ...)` source list (after `test_index.cpp`):
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
    test_utf8.cpp
    ${CORE_DIR}/src/orp.cpp
    ${CORE_DIR}/src/pacing.cpp
    ${CORE_DIR}/src/player.cpp
    ${CORE_DIR}/src/tokenize.cpp
    ${CORE_DIR}/src/index.cpp
)
```

- [ ] **Step 0.4: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed, **40 test cases** (38 + 2). `appendCodePoint` is a pure utility correct on first write — passing immediately is acceptable (the tests pin the 1–4 byte encodings as regression guards). Paste the summary lines.

- [ ] **Step 0.5: Commit**

```bash
git add core/include/rsvp/utf8.hpp test/host/test_utf8.cpp test/host/CMakeLists.txt
git commit -m "feat(core): UTF-8 code-point encoder" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 1: HTML entity decoder

**Files:**
- Create: `core/include/rsvp/entity.hpp`
- Create: `core/src/entity.cpp`
- Create: `test/host/test_entity.cpp`
- Modify: `test/host/CMakeLists.txt`

- [ ] **Step 1.1: Declare the API + a failing stub**

Create `core/include/rsvp/entity.hpp`:
```cpp
#pragma once
#include <string>

namespace rsvp {

// Decode HTML/XML character entities in s to UTF-8: &amp; &lt; &gt; &quot; &apos;,
// numeric &#NN; / &#xHH;, and common named punctuation. &nbsp; becomes a regular
// space. Unknown or malformed entities are left literal.
std::string decodeEntities(const std::string& s);

} // namespace rsvp
```

Create `core/src/entity.cpp` as a STUB (compiles, fails the tests):
```cpp
#include "rsvp/entity.hpp"

namespace rsvp {

std::string decodeEntities(const std::string& s) { return s; } // STUB: passes input through

} // namespace rsvp
```

- [ ] **Step 1.2: Write the failing tests**

Create `test/host/test_entity.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/entity.hpp"
using namespace rsvp;

TEST_CASE("decodeEntities: basic named entities") {
    CHECK(decodeEntities("Tom &amp; Jerry") == "Tom & Jerry");
    CHECK(decodeEntities("&lt;tag&gt;") == "<tag>");
    CHECK(decodeEntities("say &quot;hi&quot;") == "say \"hi\"");
    CHECK(decodeEntities("it&apos;s") == "it's");
}

TEST_CASE("decodeEntities: numeric decimal and hex") {
    CHECK(decodeEntities("&#65;&#x42;") == "AB");
    CHECK(decodeEntities("caf&#233;") == "caf\xC3\xA9"); // é
}

TEST_CASE("decodeEntities: nbsp becomes a space; punctuation entities") {
    CHECK(decodeEntities("a&nbsp;b") == "a b");
    CHECK(decodeEntities("yes&mdash;no") == "yes\xE2\x80\x94no"); // em dash U+2014
    CHECK(decodeEntities("wait&hellip;") == "wait\xE2\x80\xA6"); // ellipsis U+2026
}

TEST_CASE("decodeEntities: unknown / malformed entities are left literal") {
    CHECK(decodeEntities("A&unknown;B") == "A&unknown;B");
    CHECK(decodeEntities("100 &amp 200") == "100 &amp 200"); // no semicolon
    CHECK(decodeEntities("ends with &") == "ends with &");
    CHECK(decodeEntities("&#;") == "&#;");                   // empty numeric
}
```

- [ ] **Step 1.3: Wire into CMake**

In `test/host/CMakeLists.txt`, add `test_entity.cpp` and `${CORE_DIR}/src/entity.cpp` to the `add_executable(rsvp_tests ...)` list (alongside the other test files and core sources):
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
    test_utf8.cpp
    test_entity.cpp
    ${CORE_DIR}/src/orp.cpp
    ${CORE_DIR}/src/pacing.cpp
    ${CORE_DIR}/src/player.cpp
    ${CORE_DIR}/src/tokenize.cpp
    ${CORE_DIR}/src/index.cpp
    ${CORE_DIR}/src/entity.cpp
)
```

- [ ] **Step 1.4: Build + run → FAIL**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: FAILURE!` — the stub returns input unchanged, so every entity decode check fails (except the "left literal" cases, which happen to pass). Confirm FAILURE.

- [ ] **Step 1.5: Implement the decoder (replace the body of `core/src/entity.cpp`)**
```cpp
#include "rsvp/entity.hpp"
#include "rsvp/utf8.hpp"
#include <cstdint>

namespace rsvp {
namespace {

bool namedEntity(const std::string& name, std::uint32_t& cp) {
    struct E { const char* n; std::uint32_t c; };
    static const E table[] = {
        {"amp", 0x26}, {"lt", 0x3C}, {"gt", 0x3E}, {"quot", 0x22}, {"apos", 0x27},
        {"nbsp", 0x20}, // -> regular space (tokenization-friendly)
        {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026},
        {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
        {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122}, {"deg", 0xB0},
    };
    for (const E& e : table) if (name == e.n) { cp = e.c; return true; }
    return false;
}

bool numericEntity(const std::string& body, std::uint32_t& cp) {
    // body starts with '#'
    std::uint32_t value = 0;
    if (body.size() >= 2 && (body[1] == 'x' || body[1] == 'X')) {
        if (body.size() < 3) return false;
        for (std::size_t i = 2; i < body.size(); ++i) {
            const char c = body[i];
            std::uint32_t d;
            if (c >= '0' && c <= '9') d = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<std::uint32_t>(10 + c - 'a');
            else if (c >= 'A' && c <= 'F') d = static_cast<std::uint32_t>(10 + c - 'A');
            else return false;
            value = value * 16 + d;
            if (value > 0x10FFFF) return false;
        }
    } else {
        if (body.size() < 2) return false;
        for (std::size_t i = 1; i < body.size(); ++i) {
            const char c = body[i];
            if (c < '0' || c > '9') return false;
            value = value * 10 + static_cast<std::uint32_t>(c - '0');
            if (value > 0x10FFFF) return false;
        }
    }
    cp = value;
    return true;
}

} // namespace

std::string decodeEntities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        if (s[i] != '&') { out.push_back(s[i]); ++i; continue; }
        const std::size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 32) { out.push_back('&'); ++i; continue; }
        const std::string body = s.substr(i + 1, semi - i - 1);
        std::uint32_t cp = 0;
        const bool ok = (!body.empty() && body[0] == '#') ? numericEntity(body, cp)
                                                          : namedEntity(body, cp);
        if (ok) { utf8::appendCodePoint(out, cp); i = semi + 1; }
        else    { out.push_back('&'); ++i; }
    }
    return out;
}

} // namespace rsvp
```

- [ ] **Step 1.6: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed, **44 test cases** (40 + 4). Paste the summary lines.

- [ ] **Step 1.7: Commit**

```bash
git add core/include/rsvp/entity.hpp core/src/entity.cpp test/host/test_entity.cpp test/host/CMakeLists.txt
git commit -m "feat(core): HTML entity decoder" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: htmlToText extractor (+ end-to-end test)

**Files:**
- Create: `core/include/rsvp/htmltext.hpp`
- Create: `core/src/htmltext.cpp`
- Create: `test/host/test_htmltext.cpp`
- Modify: `test/host/CMakeLists.txt`

- [ ] **Step 2.1: Declare the API + a failing stub**

Create `core/include/rsvp/htmltext.hpp`:
```cpp
#pragma once
#include <string>

namespace rsvp {

// Convert XHTML/HTML to plain reading text: tags removed, entities decoded,
// <script>/<style> content dropped, block elements separated by blank lines
// (paragraphs), inline tags and <br> reduced to a space, whitespace normalized.
// The output is suitable input for tokenizePlainText.
std::string htmlToText(const std::string& html);

} // namespace rsvp
```

Create `core/src/htmltext.cpp` as a STUB:
```cpp
#include "rsvp/htmltext.hpp"

namespace rsvp {

std::string htmlToText(const std::string& html) { return html; } // STUB: returns input unchanged

} // namespace rsvp
```

- [ ] **Step 2.2: Write the failing tests**

Create `test/host/test_htmltext.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/htmltext.hpp"
#include "rsvp/tokenize.hpp"
using namespace rsvp;

TEST_CASE("htmlToText: strips inline tags, keeps text") {
    CHECK(htmlToText("<p>Hello <b>brave</b> world.</p>") == "Hello brave world.");
}

TEST_CASE("htmlToText: block elements become paragraph breaks") {
    CHECK(htmlToText("<p>One.</p><p>Two.</p>") == "One.\n\nTwo.");
    CHECK(htmlToText("<h1>Title</h1><p>Body here.</p>") == "Title\n\nBody here.");
}

TEST_CASE("htmlToText: decodes entities and collapses source whitespace") {
    CHECK(htmlToText("<p>Tom &amp; Jerry   are\n\n  friends.</p>") == "Tom & Jerry are friends.");
}

TEST_CASE("htmlToText: drops script and style content") {
    CHECK(htmlToText("<style>p{color:red}</style><p>Hi.</p>") == "Hi.");
    CHECK(htmlToText("<script>var a='<p>x</p>';</script><p>Real.</p>") == "Real.");
}

TEST_CASE("htmlToText: <br> is a soft space, not a paragraph") {
    CHECK(htmlToText("<p>line one<br>line two</p>") == "line one line two");
}

TEST_CASE("html pipeline: htmlToText feeds tokenizePlainText into a Document") {
    Document d = tokenizePlainText(htmlToText("<h1>Ch</h1><p>The cat sat.</p><p>It ran.</p>"));
    // text == "Ch\n\nThe cat sat.\n\nIt ran." -> tokens: Ch, The, cat, sat., It, ran.
    REQUIRE(d.size() == 6);
    CHECK(d.tokens[0].text == "Ch");
    CHECK(d.tokens[0].has(FLAG_PARAGRAPH_END));   // "Ch" precedes a blank line
    CHECK(d.tokens[3].text == "sat.");
    CHECK(d.tokens[3].has(FLAG_SENTENCE_END));
    CHECK(d.tokens[3].has(FLAG_PARAGRAPH_END));
    CHECK(d.tokens[5].text == "ran.");
    CHECK(d.tokens[5].has(FLAG_SENTENCE_END));
    CHECK_FALSE(d.tokens[5].has(FLAG_PARAGRAPH_END)); // last word
}
```

- [ ] **Step 2.3: Wire into CMake**

In `test/host/CMakeLists.txt`, add `test_htmltext.cpp` and `${CORE_DIR}/src/htmltext.cpp` to the `add_executable(rsvp_tests ...)` list:
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
    test_utf8.cpp
    test_entity.cpp
    test_htmltext.cpp
    ${CORE_DIR}/src/orp.cpp
    ${CORE_DIR}/src/pacing.cpp
    ${CORE_DIR}/src/player.cpp
    ${CORE_DIR}/src/tokenize.cpp
    ${CORE_DIR}/src/index.cpp
    ${CORE_DIR}/src/entity.cpp
    ${CORE_DIR}/src/htmltext.cpp
)
```

- [ ] **Step 2.4: Build + run → FAIL**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: FAILURE!` — the stub returns the raw HTML, so the htmlToText cases and the pipeline case fail. Confirm FAILURE.

- [ ] **Step 2.5: Implement the extractor (replace the body of `core/src/htmltext.cpp`)**
```cpp
#include "rsvp/htmltext.hpp"
#include "rsvp/entity.hpp"

namespace rsvp {
namespace {

bool isHSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool isBlockTag(const std::string& name) {
    static const char* blocks[] = {
        "p","div","h1","h2","h3","h4","h5","h6","li","ul","ol","blockquote",
        "section","article","header","footer","figure","figcaption","pre","hr",
        "table","tr","td","th","thead","tbody","body","main","aside","nav",
        "dd","dt","dl","address","fieldset","form","caption","colgroup"
    };
    for (const char* b : blocks) if (name == b) return true;
    return false;
}

// Parse a tag's lowercased name from the text between '<' and '>'. Sets isEnd for a
// closing tag. Returns "" for comments / doctype / processing instructions.
std::string tagName(const std::string& inner, bool& isEnd) {
    isEnd = false;
    std::size_t i = 0;
    const std::size_t n = inner.size();
    while (i < n && isHSpace(static_cast<unsigned char>(inner[i]))) ++i;
    if (i < n && inner[i] == '/') { isEnd = true; ++i; }
    if (i < n && (inner[i] == '!' || inner[i] == '?')) return "";
    std::string name;
    while (i < n) {
        const char c = inner[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            name.push_back(lower(c)); ++i;
        } else break;
    }
    return name;
}

// Case-insensitive search for needle in hay starting at from.
std::size_t ifind(const std::string& hay, const std::string& needle, std::size_t from) {
    if (needle.empty() || from > hay.size()) return std::string::npos;
    for (std::size_t i = from; i + needle.size() <= hay.size(); ++i) {
        std::size_t j = 0;
        for (; j < needle.size(); ++j) if (lower(hay[i + j]) != lower(needle[j])) break;
        if (j == needle.size()) return i;
    }
    return std::string::npos;
}

// Collapse whitespace: runs without a newline -> a single space; runs containing a
// newline -> a paragraph break ("\n\n"). Leading/trailing whitespace trimmed.
std::string normalizeWhitespace(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '\n' || isHSpace(c)) {
            bool hasNewline = false;
            while (i < n && (s[i] == '\n' || isHSpace(static_cast<unsigned char>(s[i])))) {
                if (s[i] == '\n') hasNewline = true;
                ++i;
            }
            if (!out.empty()) out += hasNewline ? "\n\n" : " ";
        } else {
            out.push_back(static_cast<char>(c));
            ++i;
        }
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

} // namespace

std::string htmlToText(const std::string& html) {
    // Phase 1: strip tags into `raw`. Block boundaries become '\n', <br> and source
    // whitespace become ' ', script/style content is dropped, inline tags vanish.
    std::string raw;
    raw.reserve(html.size());
    const std::size_t n = html.size();
    std::size_t i = 0;
    while (i < n) {
        const char ch = html[i];
        if (ch == '<') {
            const std::size_t end = html.find('>', i + 1);
            if (end == std::string::npos) break; // unterminated tag: drop the rest
            const std::string inner = html.substr(i + 1, end - i - 1);
            bool isEnd = false;
            const std::string name = tagName(inner, isEnd);
            if (!isEnd && (name == "script" || name == "style")) {
                const std::string close = "</" + name + ">";
                const std::size_t cpos = ifind(html, close, end + 1);
                i = (cpos == std::string::npos) ? n : cpos + close.size();
                continue;
            }
            if (name == "br") raw.push_back(' ');
            else if (isBlockTag(name)) raw.push_back('\n');
            // inline tags: removed
            i = end + 1;
            continue;
        }
        if (ch == '\n' || isHSpace(static_cast<unsigned char>(ch))) raw.push_back(' ');
        else raw.push_back(ch);
        ++i;
    }
    // Phase 2: decode entities, then normalize whitespace into paragraphs.
    return normalizeWhitespace(decodeEntities(raw));
}

} // namespace rsvp
```

- [ ] **Step 2.6: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed, **50 test cases** (44 + 6). Paste the summary lines.

- [ ] **Step 2.7: Commit**

```bash
git add core/include/rsvp/htmltext.hpp core/src/htmltext.cpp test/host/test_htmltext.cpp test/host/CMakeLists.txt
git commit -m "feat(core): XHTML-to-text extractor feeding the tokenizer" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-review (performed against the spec)

**1. Spec coverage (the EPUB "XHTML → text" step, §4):**
- Tag stripping with block→paragraph structure → Task 2 (`htmlToText`). ✓
- Entity decoding (named + numeric) → Task 1; UTF-8 encoding for it → Task 0. ✓
- script/style removal, whitespace normalization, `<br>` handling → Task 2. ✓
- Composes with the existing tokenizer → Task 2 end-to-end test. ✓
- *Deferred (named in Scope):* ZIP/miniz, OPF/container parsing, spine assembly, chapter-from-heading, EPUB-file indexer — correctly out of this plan.

**2. Placeholder scan:** No "TBD/TODO/handle X". Every code step is complete; stubs are explicit and replaced in the named step. ✓

**3. Type consistency:** `utf8::appendCodePoint(std::string&, std::uint32_t)`, `decodeEntities(const std::string&)`, `htmlToText(const std::string&)`, and the reuse of `tokenizePlainText`/`Document`/`FLAG_*` are consistent across headers, sources, and tests. `htmltext.cpp` calls `decodeEntities` (Task 1) and is exercised with `tokenizePlainText` (Plan 2). ✓

---

## Execution handoff

Implement task-by-task with `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans`, branching `feature/rsvp-html-extract` off `feature/rsvp-doc-pipeline`. After Task 2, XHTML flows end-to-end into a `Document`, ready for the (later) EPUB plan to unzip a book, parse its OPF/spine, run each spine document through `htmlToText`, and serialize via the Plan 2 codec.

# EPUB OPF / Container Parsing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parse an EPUB's `META-INF/container.xml` and OPF package document into structured data — book title/author, the ordered reading-spine (resolved to content hrefs), and the cover href — as pure host-tested logic.

**Architecture:** One `opf` unit added to `core/`: a generic XML attribute extractor (`attrValue`), a `container.xml` reader (`parseContainerOpfPath`), and the OPF parser (`parseOpf`) built on small file-local tag/element helpers. Namespace-prefix-insensitive (`dc:title` ≡ `title`), entity-decoding (reuses Plan 3's `decodeEntities`). No ZIP, no hardware — the ZIP layer + end-to-end EPUB indexer is Plan 5. Same host harness (doctest, MSVC/VS 2022). Strict TDD.

**Tech Stack:** C++17, CMake with the Visual Studio 2022 (MSVC) generator, doctest v2.4.11. Reuses `rsvp::decodeEntities`.

---

## Scope & rationale

Per the design spec's EPUB indexer (§4), indexing a book requires its OPF: the reading order (spine), metadata, and cover. This plan implements that parsing from XML strings — fully host-testable with OPF/container fixtures, no ZIP library or binary EPUB needed. Plan 5 will unzip a real EPUB, hand the OPF + each spine document's XHTML to this code and to `htmlToText`, and serialize via the Plan 2 codec.

**In scope:** `attrValue`; `parseContainerOpfPath`; `parseOpf` (title, author, ordered spine hrefs, cover href; EPUB2 `<meta name="cover">` and EPUB3 `properties="cover-image"`).
**Out of scope (Plan 5+):** ZIP/`miniz`, reading spine XHTML files, href path resolution against the OPF directory, chapter-from-NCX/nav, the end-to-end EPUB indexer, hardware/SD.

**Dependency / branch:** Reuses Plan 3's `decodeEntities` (on `feature/rsvp-html-extract`). Execution branches **`feature/rsvp-epub-opf` off `feature/rsvp-html-extract`**.

## Design decisions (please review before execution)

1. **Namespace-insensitive local names:** tag local name = the part after `:` (`dc:title` → `title`, `opf:item` → `item`), lowercased. So the parser ignores XML namespace prefixes (which vary across books).
2. **Best-effort, not validating:** malformed/garbage OPF yields empty/partial `OpfData` (no exceptions), matching the MCU-friendly style. Missing title/author/cover → empty strings; missing spine → empty list.
3. **Spine resolution:** each `<itemref idref="X"/>` resolves to the `<item id="X" href="...">`'s href, preserving spine order. Unresolved idrefs are skipped.
4. **Cover:** EPUB3 `<item ... properties="cover-image">`'s href wins; else EPUB2 `<meta name="cover" content="ID"/>` → that item's href; else "".
5. **Entities:** title/author text is entity-decoded (`&amp;` → `&`). Attribute values are returned raw (hrefs/ids rarely contain entities; the ZIP layer resolves them as-is).
6. **Hrefs returned as-authored** (relative to the OPF file). Resolving them against the OPF directory is Plan 5's job (it knows the OPF path).

## File structure

```
core/include/rsvp/opf.hpp   — attrValue, parseContainerOpfPath, OpfData, parseOpf
core/src/opf.cpp            — impls (+ file-local tag/element helpers)
test/host/test_opf.cpp      — tests
test/host/CMakeLists.txt    — MODIFIED: add the new source + test
```

## Conventions

- **TDD:** failing test → watch it fail → minimal impl → watch it pass → commit.
- **Toolchain:** `build/host/` is configured with `-G "Visual Studio 17 2022"`. Build `cmake --build build/host`; run `./build/host/Debug/rsvp_tests.exe`.
- **Commit trailer:** end every commit with `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
- **Branch:** `feature/rsvp-epub-opf` off `feature/rsvp-html-extract`. Do not implement on `main`.
- **Counts:** suite starts at **53** cases. Key signal is **`Status: SUCCESS!` with 0 failed**.

---

### Task 0: `attrValue` — XML attribute extractor

**Files:**
- Create: `core/include/rsvp/opf.hpp`
- Create: `core/src/opf.cpp`
- Create: `test/host/test_opf.cpp`
- Modify: `test/host/CMakeLists.txt`

- [ ] **Step 0.1: Declare the full `opf` API + stubs**

Create `core/include/rsvp/opf.hpp`:
```cpp
#pragma once
#include <string>
#include <vector>

namespace rsvp {

// Extract attribute `name`'s value from a tag's inner text (name="v" or name='v').
// Returns "" if absent. Entities are NOT decoded. Matches whole attribute names only.
std::string attrValue(const std::string& tagInner, const std::string& name);

// From an EPUB META-INF/container.xml, return the OPF package's full-path ("" if none).
std::string parseContainerOpfPath(const std::string& containerXml);

// Parsed OPF package document.
struct OpfData {
    std::string              title;
    std::string              author;
    std::vector<std::string> spineHrefs;  // reading order (manifest hrefs, OPF-relative)
    std::string              coverHref;    // "" if none
};

OpfData parseOpf(const std::string& opfXml);

} // namespace rsvp
```

Create `core/src/opf.cpp` with `attrValue` implemented and the other two STUBBED:
```cpp
#include "rsvp/opf.hpp"

namespace rsvp {
namespace {
bool isWs(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }
} // namespace

std::string attrValue(const std::string& s, const std::string& name) {
    const std::size_t n = s.size(), m = name.size();
    if (m == 0) return "";
    std::size_t i = 0;
    while (i < n) {
        const std::size_t pos = s.find(name, i);
        if (pos == std::string::npos) return "";
        const bool leftOk = (pos == 0) || isWs(s[pos - 1]);
        std::size_t j = pos + m;
        while (j < n && isWs(s[j])) ++j;
        if (leftOk && j < n && s[j] == '=') {
            ++j;
            while (j < n && isWs(s[j])) ++j;
            if (j < n && (s[j] == '"' || s[j] == '\'')) {
                const char q = s[j++];
                const std::size_t start = j;
                while (j < n && s[j] != q) ++j;
                return s.substr(start, j - start);
            }
        }
        i = pos + 1;
    }
    return "";
}

// --- stubs replaced in Tasks 1 and 2 ---
std::string parseContainerOpfPath(const std::string&) { return ""; }
OpfData parseOpf(const std::string&) { return OpfData{}; }

} // namespace rsvp
```

- [ ] **Step 0.2: Write the failing tests**

Create `test/host/test_opf.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/opf.hpp"
using namespace rsvp;

TEST_CASE("attrValue: extracts double- and single-quoted values") {
    CHECK(attrValue("item id=\"x\" href=\"a.xhtml\"", "href") == "a.xhtml");
    CHECK(attrValue("rootfile full-path='OEBPS/content.opf'", "full-path") == "OEBPS/content.opf");
}

TEST_CASE("attrValue: respects name boundaries and missing attrs") {
    CHECK(attrValue("a hrefx=\"no\" href=\"yes\"", "href") == "yes");
    CHECK(attrValue("itemref idref=\"ch1\"", "id") == "");          // 'id' must not match 'idref'
    CHECK(attrValue("item id=\"x\"", "href") == "");                // absent
    CHECK(attrValue("itemref idref = \"ch1\"", "idref") == "ch1");  // spaces around '='
}
```

- [ ] **Step 0.3: Wire into CMake**

In `test/host/CMakeLists.txt`, add `test_opf.cpp` and `${CORE_DIR}/src/opf.cpp` to `add_executable(rsvp_tests ...)`. The resulting block (the current list already ends with the Plan 3 sources):
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
    test_opf.cpp
    ${CORE_DIR}/src/orp.cpp
    ${CORE_DIR}/src/pacing.cpp
    ${CORE_DIR}/src/player.cpp
    ${CORE_DIR}/src/tokenize.cpp
    ${CORE_DIR}/src/index.cpp
    ${CORE_DIR}/src/entity.cpp
    ${CORE_DIR}/src/htmltext.cpp
    ${CORE_DIR}/src/opf.cpp
)
```

- [ ] **Step 0.4: Build + run → FAIL**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: FAILURE!` — the `opf.cpp` does not yet exist before you create it, so the first build fails to compile; once created with `attrValue` as written it actually PASSES. **If you wrote `attrValue` correctly in 0.1, this step instead shows SUCCESS at 55 cases** — that's acceptable for this pure utility (the value is the regression guard). Note whichever you observe and proceed.

- [ ] **Step 0.5: (If 0.4 failed to compile or any attrValue test failed) ensure `attrValue` matches Step 0.1, then build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed, **55 test cases** (53 + 2). Paste the summary lines.

- [ ] **Step 0.6: Commit**

```bash
git add core/include/rsvp/opf.hpp core/src/opf.cpp test/host/test_opf.cpp test/host/CMakeLists.txt
git commit -m "feat(core): XML attribute extractor (attrValue) for OPF parsing" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 1: `parseContainerOpfPath` — read the OPF path from container.xml

**Files:**
- Modify: `core/src/opf.cpp` (replace the `parseContainerOpfPath` stub)
- Modify: `test/host/test_opf.cpp` (add tests)

- [ ] **Step 1.1: Write the failing tests**

Append to `test/host/test_opf.cpp`:
```cpp
TEST_CASE("parseContainerOpfPath: returns the OPF rootfile full-path") {
    std::string xml =
        "<?xml version=\"1.0\"?>\n"
        "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
        "  <rootfiles>\n"
        "    <rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>\n"
        "  </rootfiles>\n"
        "</container>";
    CHECK(parseContainerOpfPath(xml) == "OEBPS/content.opf");
    CHECK(parseContainerOpfPath("<container></container>") == "");
}
```

- [ ] **Step 1.2: Build + run → FAIL**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: FAILURE!` — the stub returns "" so the first CHECK fails (the empty-container CHECK passes). Confirm FAILURE.

- [ ] **Step 1.3: Implement `parseContainerOpfPath`**

In `core/src/opf.cpp`, replace the stub line:
```cpp
std::string parseContainerOpfPath(const std::string&) { return ""; }
```
with:
```cpp
std::string parseContainerOpfPath(const std::string& xml) {
    const std::size_t pos = xml.find("<rootfile");
    if (pos == std::string::npos) return "";
    const std::size_t end = xml.find('>', pos);
    if (end == std::string::npos) return "";
    return attrValue(xml.substr(pos, end - pos), "full-path");
}
```

- [ ] **Step 1.4: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed, **56 test cases** (55 + 1). Paste the summary lines.

- [ ] **Step 1.5: Commit**

```bash
git add core/src/opf.cpp test/host/test_opf.cpp
git commit -m "feat(core): parse OPF path from EPUB container.xml" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: `parseOpf` — metadata, spine order, cover

**Files:**
- Modify: `core/src/opf.cpp` (add helpers; replace the `parseOpf` stub)
- Modify: `test/host/test_opf.cpp` (add tests)

- [ ] **Step 2.1: Write the failing tests**

Append to `test/host/test_opf.cpp`:
```cpp
TEST_CASE("parseOpf: extracts title, author, spine order, and EPUB3 cover") {
    std::string opf =
        "<?xml version=\"1.0\"?>\n"
        "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\">\n"
        "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
        "    <dc:title>The Great Book</dc:title>\n"
        "    <dc:creator>Jane Doe</dc:creator>\n"
        "  </metadata>\n"
        "  <manifest>\n"
        "    <item id=\"cover\" href=\"cover.jpg\" media-type=\"image/jpeg\" properties=\"cover-image\"/>\n"
        "    <item id=\"ch1\" href=\"chap1.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
        "    <item id=\"ch2\" href=\"chap2.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
        "  </manifest>\n"
        "  <spine>\n"
        "    <itemref idref=\"ch1\"/>\n"
        "    <itemref idref=\"ch2\"/>\n"
        "  </spine>\n"
        "</package>";
    OpfData d = parseOpf(opf);
    CHECK(d.title == "The Great Book");
    CHECK(d.author == "Jane Doe");
    REQUIRE(d.spineHrefs.size() == 2);
    CHECK(d.spineHrefs[0] == "chap1.xhtml");
    CHECK(d.spineHrefs[1] == "chap2.xhtml");
    CHECK(d.coverHref == "cover.jpg");
}

TEST_CASE("parseOpf: EPUB2 cover via <meta name=cover>; decodes title entities") {
    std::string opf =
        "<package>\n"
        "  <metadata>\n"
        "    <dc:title>Tom &amp; Jerry</dc:title>\n"
        "    <meta name=\"cover\" content=\"coverimg\"/>\n"
        "  </metadata>\n"
        "  <manifest>\n"
        "    <item id=\"coverimg\" href=\"images/c.png\" media-type=\"image/png\"/>\n"
        "    <item id=\"t1\" href=\"text1.html\" media-type=\"application/xhtml+xml\"/>\n"
        "  </manifest>\n"
        "  <spine><itemref idref=\"t1\"/></spine>\n"
        "</package>";
    OpfData d = parseOpf(opf);
    CHECK(d.title == "Tom & Jerry");       // entity-decoded
    REQUIRE(d.spineHrefs.size() == 1);
    CHECK(d.spineHrefs[0] == "text1.html");
    CHECK(d.coverHref == "images/c.png");  // resolved via meta name=cover
}

TEST_CASE("parseOpf: empty / structureless input yields empty data") {
    OpfData d = parseOpf("<package></package>");
    CHECK(d.title == "");
    CHECK(d.author == "");
    CHECK(d.spineHrefs.empty());
    CHECK(d.coverHref == "");
}
```

- [ ] **Step 2.2: Build + run → FAIL**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: FAILURE!` — the stub returns an empty `OpfData`, so the title/spine/cover checks in the first two cases fail (the empty-input case passes). Confirm FAILURE.

- [ ] **Step 2.3: Implement `parseOpf` and its helpers**

In `core/src/opf.cpp`, first add `#include "rsvp/entity.hpp"` and `#include <vector>` at the top (after the existing `#include "rsvp/opf.hpp"`):
```cpp
#include "rsvp/opf.hpp"
#include "rsvp/entity.hpp"
#include <vector>
```

Then add these helpers inside the existing anonymous `namespace { ... }` (after `isWs`):
```cpp
// Local tag name (namespace prefix stripped, lowercased) from a tag's inner text.
std::string readLocalTagName(const std::string& inner, bool& isEnd, bool& selfClose) {
    isEnd = false;
    selfClose = !inner.empty() && inner.back() == '/';
    std::size_t i = 0;
    const std::size_t n = inner.size();
    while (i < n && isWs(inner[i])) ++i;
    if (i < n && inner[i] == '/') { isEnd = true; ++i; }
    if (i < n && (inner[i] == '!' || inner[i] == '?')) return "";
    std::string name;
    while (i < n) {
        const char c = inner[i];
        if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==':'||c=='-'||c=='_') {
            name.push_back(c); ++i;
        } else break;
    }
    const std::size_t colon = name.find(':');
    if (colon != std::string::npos) name = name.substr(colon + 1);
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return name;
}

// The inner text (name + attributes) of every non-end tag whose local name == local.
std::vector<std::string> tagInners(const std::string& xml, const std::string& local) {
    std::vector<std::string> out;
    std::size_t i = 0;
    const std::size_t n = xml.size();
    while (i < n) {
        const std::size_t lt = xml.find('<', i); if (lt == std::string::npos) break;
        const std::size_t gt = xml.find('>', lt + 1); if (gt == std::string::npos) break;
        const std::string inner = xml.substr(lt + 1, gt - lt - 1);
        bool isEnd = false, selfClose = false;
        if (readLocalTagName(inner, isEnd, selfClose) == local && !isEnd) out.push_back(inner);
        i = gt + 1;
    }
    return out;
}

// Trimmed, entity-decoded text of the first non-end, non-self-closing element with local name == local.
std::string elementText(const std::string& xml, const std::string& local) {
    std::size_t i = 0;
    const std::size_t n = xml.size();
    while (i < n) {
        const std::size_t lt = xml.find('<', i); if (lt == std::string::npos) return "";
        const std::size_t gt = xml.find('>', lt + 1); if (gt == std::string::npos) return "";
        const std::string inner = xml.substr(lt + 1, gt - lt - 1);
        bool isEnd = false, selfClose = false;
        if (readLocalTagName(inner, isEnd, selfClose) == local && !isEnd && !selfClose) {
            const std::size_t te = xml.find('<', gt + 1);
            const std::string text = xml.substr(gt + 1, (te == std::string::npos ? n : te) - (gt + 1));
            std::size_t a = 0, b = text.size();
            while (a < b && isWs(text[a])) ++a;
            while (b > a && isWs(text[b - 1])) --b;
            return decodeEntities(text.substr(a, b - a));
        }
        i = gt + 1;
    }
    return "";
}

bool containsStr(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}
```

Then replace the stub line:
```cpp
OpfData parseOpf(const std::string&) { return OpfData{}; }
```
with:
```cpp
OpfData parseOpf(const std::string& xml) {
    OpfData d;
    d.title  = elementText(xml, "title");
    d.author = elementText(xml, "creator");

    // Manifest: collect id/href pairs; note an EPUB3 cover-image href.
    std::vector<std::string> ids, hrefs;
    std::string coverFromProps;
    for (const std::string& it : tagInners(xml, "item")) {
        const std::string id = attrValue(it, "id");
        const std::string href = attrValue(it, "href");
        if (id.empty() || href.empty()) continue;
        ids.push_back(id);
        hrefs.push_back(href);
        if (containsStr(attrValue(it, "properties"), "cover-image")) coverFromProps = href;
    }
    auto hrefForId = [&](const std::string& id) -> std::string {
        for (std::size_t k = 0; k < ids.size(); ++k) if (ids[k] == id) return hrefs[k];
        return "";
    };

    // EPUB2 cover fallback: <meta name="cover" content="ID">.
    std::string coverFromMeta;
    for (const std::string& mt : tagInners(xml, "meta")) {
        if (attrValue(mt, "name") == "cover") { coverFromMeta = hrefForId(attrValue(mt, "content")); break; }
    }

    // Spine: resolve each itemref's idref to its manifest href, in order.
    for (const std::string& ir : tagInners(xml, "itemref")) {
        const std::string href = hrefForId(attrValue(ir, "idref"));
        if (!href.empty()) d.spineHrefs.push_back(href);
    }

    d.coverHref = !coverFromProps.empty() ? coverFromProps : coverFromMeta;
    return d;
}
```

- [ ] **Step 2.4: Build + run → PASS**

Run:
```bash
cmake --build build/host
./build/host/Debug/rsvp_tests.exe
```
Expected: `Status: SUCCESS!`, 0 failed, **59 test cases** (56 + 3). Paste the summary lines.

- [ ] **Step 2.5: Commit**

```bash
git add core/src/opf.cpp test/host/test_opf.cpp
git commit -m "feat(core): OPF parser (title, author, spine order, cover)" -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-review (performed against the spec)

**1. Spec coverage (EPUB indexer structure/metadata, §4):**
- OPF path from container.xml → Task 1. ✓
- Reading order (spine → manifest hrefs) → Task 2. ✓
- Title/author metadata (entity-decoded, namespace-insensitive) → Task 2. ✓
- Cover (EPUB3 properties + EPUB2 meta) → Task 2. ✓
- Generic attribute extraction → Task 0. ✓
- *Deferred (named in Scope):* ZIP/miniz, reading spine XHTML, OPF-relative href resolution, NCX/nav chapters, end-to-end indexer — correctly Plan 5+.

**2. Placeholder scan:** No "TBD/TODO". Every code step is complete; stubs are explicit and replaced in the named step.

**3. Type consistency:** `attrValue(const std::string&, const std::string&)`, `parseContainerOpfPath(const std::string&)`, `OpfData{title,author,spineHrefs,coverHref}`, `parseOpf(const std::string&)`, and the file-local `readLocalTagName/tagInners/elementText/containsStr` are used consistently across header, source, and tests. `elementText` reuses `decodeEntities` (Plan 3). ✓

---

## Execution handoff

Implement task-by-task with `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans`, branching `feature/rsvp-epub-opf` off `feature/rsvp-html-extract`. After Task 2, an OPF + container.xml parse into a book's metadata, ordered spine, and cover — ready for Plan 5 to unzip the EPUB, feed each spine doc through `htmlToText`, and serialize the result with the Plan 2 codec.

# Bounded-Memory Book Loading (Streaming Index + On-Demand Reader) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load full-length novels from SD without OOM by streaming the compile into the compact index and reading on demand from `CompiledIndex`, behind a Loading screen.

**Architecture:** Add an incremental `IndexBuilder` (serialize tokens as they're produced, never holding the ~5 MB `Document`); a streaming `tokenizePlainTextInto`; refactor `epubToIndex` to feed the builder per chapter; change `Player` to read from `CompiledIndex.at(i)`; run the compile in a big-stack FreeRTOS task with a Loading screen swapped to the reader by an `lv_timer`. Back-compat wrappers keep existing host tests valid.

**Tech Stack:** C++17 `core` (doctest host tests), ESP-IDF 5.5.2 + LVGL 9.

---

## Conventions

**[HOST-TEST]**: `cmake --build "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\build\host" --config Debug` then `& "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\build\host\Debug\rsvp_tests.exe"`

**[BUILD]/[FLASH]**: `$idf="C:\Users\WilliamHerr\esp\v5.5.2\esp-idf"; $env:IDF_PATH=$idf; $env:IDF_PYTHON_ENV_PATH="C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env"; & "$idf\export.ps1" *> $null; idf.py -C "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\firmware" build` (flash: append `-p COM32 flash`).

**[CAPTURE n]**: `& "C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe" "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader\firmware\build\cap.py" n` (reset-and-capture helper already present).

**Prereq:** the exFAT card with Project Hail Mary at root (already in the device).

---

## Task 1: `IndexBuilder` + reimplement `serializeIndex` on it

**Files:** Create `core/include/rsvp/indexbuilder.hpp`, `core/src/indexbuilder.cpp`, `test/host/test_indexbuilder.cpp`; Modify `core/src/index.cpp`, `core/CMakeLists.txt`, `test/host/CMakeLists.txt`.

- [ ] **Step 1: Header** — `core/include/rsvp/indexbuilder.hpp`:
```cpp
#pragma once
#include "rsvp/index.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace rsvp {

// Builds the compiled-index byte format incrementally: feed tokens (and chapter
// marks) as they are produced, holding only the serialized stream + seek table,
// never the full Document. Output is byte-identical to serializeIndex.
class IndexBuilder {
public:
    explicit IndexBuilder(const DocMeta& meta, std::uint32_t seekInterval = kDefaultSeekInterval);
    void startChapter(const std::string& title = std::string());  // marks a chapter at the current word
    void addToken(const std::string& text, std::uint8_t flags);
    std::vector<std::uint8_t> finish();

private:
    DocMeta                    meta_;
    std::uint32_t              seekInterval_;
    std::uint32_t              wordCount_ = 0;
    std::vector<std::uint8_t>  tokenStream_;
    std::vector<std::uint32_t> seekOffsets_;
    std::vector<Chapter>       chapters_;
};

} // namespace rsvp
```

- [ ] **Step 2: Failing test** — `test/host/test_indexbuilder.cpp`:
```cpp
#include "doctest.h"
#include "rsvp/indexbuilder.hpp"
#include "rsvp/index.hpp"
using namespace rsvp;

TEST_CASE("IndexBuilder round-trips tokens, chapters, meta via CompiledIndex") {
    DocMeta m; m.title = "T"; m.author = "A"; m.sourceSize = 10; m.sourceMtime = 20;
    IndexBuilder ib(m, 4);
    ib.startChapter("Ch1");
    ib.addToken("hello", FLAG_NONE);
    ib.addToken("world.", FLAG_SENTENCE_END);
    ib.startChapter("Ch2");
    ib.addToken("next", FLAG_CHAPTER_START);

    auto ci = CompiledIndex::parse(ib.finish());
    REQUIRE(ci.ok());
    CHECK(ci.wordCount() == 3);
    CHECK(ci.meta().title == "T");
    CHECK(ci.meta().sourceSize == 10);
    CHECK(ci.at(0).text == "hello");
    CHECK(ci.at(1).text == "world.");
    CHECK(ci.at(1).has(FLAG_SENTENCE_END));
    CHECK(ci.at(2).text == "next");
    REQUIRE(ci.chapters().size() == 2);
    CHECK(ci.chapters()[0].wordOffset == 0);
    CHECK(ci.chapters()[0].title == "Ch1");
    CHECK(ci.chapters()[1].wordOffset == 2);
}

TEST_CASE("serializeIndex output equals the equivalent IndexBuilder output") {
    DocMeta m; m.title = "Bk"; m.author = "Au"; m.sourceSize = 1; m.sourceMtime = 2;
    Document doc;
    doc.tokens = { Token{"a", FLAG_NONE}, Token{"b.", FLAG_SENTENCE_END}, Token{"c", FLAG_NONE} };
    std::vector<Chapter> chs = { Chapter{0, "One"}, Chapter{2, "Two"} };

    IndexBuilder ib(m);
    ib.startChapter("One");
    ib.addToken("a", FLAG_NONE);
    ib.addToken("b.", FLAG_SENTENCE_END);
    ib.startChapter("Two");
    ib.addToken("c", FLAG_NONE);

    CHECK(serializeIndex(doc, m, chs) == ib.finish());   // byte-identical
}
```

- [ ] **Step 3: Register** — in `core/CMakeLists.txt` `SRCS` add `"src/indexbuilder.cpp"` (after `"src/index.cpp"`). In `test/host/CMakeLists.txt` add `test_indexbuilder.cpp` (after `test_index.cpp`) and `${CORE_DIR}/src/indexbuilder.cpp` (after `${CORE_DIR}/src/index.cpp`).

- [ ] **Step 4: Run, expect red** — **[HOST-TEST]**: build FAILS (no `IndexBuilder` impl).

- [ ] **Step 5: Implement** — `core/src/indexbuilder.cpp`:
```cpp
#include "rsvp/indexbuilder.hpp"
#include "rsvp/byteio.hpp"

namespace rsvp {
using namespace byteio;

namespace {
void putBlob16(std::vector<std::uint8_t>& b, const std::string& s) {
    std::size_t len = s.size();
    if (len > 0xFFFFu) len = 0xFFFFu;
    putU16(b, static_cast<std::uint16_t>(len));
    b.insert(b.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(len));
}
constexpr char kMagic[4] = {'R', 'S', 'V', 'I'};
constexpr std::uint16_t kVersion = 1;
} // namespace

IndexBuilder::IndexBuilder(const DocMeta& meta, std::uint32_t seekInterval)
    : meta_(meta), seekInterval_(seekInterval == 0 ? kDefaultSeekInterval : seekInterval) {}

void IndexBuilder::startChapter(const std::string& title) {
    chapters_.push_back(Chapter{ wordCount_, title });
}

void IndexBuilder::addToken(const std::string& text, std::uint8_t flags) {
    if (wordCount_ % seekInterval_ == 0)
        seekOffsets_.push_back(static_cast<std::uint32_t>(tokenStream_.size()));
    tokenStream_.push_back(flags);
    putBlob16(tokenStream_, text);
    ++wordCount_;
}

std::vector<std::uint8_t> IndexBuilder::finish() {
    std::vector<std::uint8_t> b;
    b.insert(b.end(), kMagic, kMagic + 4);
    putU16(b, kVersion);
    putU16(b, 0);
    putU32(b, meta_.sourceSize);
    putU32(b, meta_.sourceMtime);
    putU32(b, wordCount_);
    putU32(b, static_cast<std::uint32_t>(chapters_.size()));
    putU32(b, seekInterval_);
    putBlob16(b, meta_.title);
    putBlob16(b, meta_.author);
    for (const Chapter& c : chapters_) { putU32(b, c.wordOffset); putBlob16(b, c.title); }
    putU32(b, static_cast<std::uint32_t>(seekOffsets_.size()));
    for (std::uint32_t off : seekOffsets_) putU32(b, off);
    b.insert(b.end(), tokenStream_.begin(), tokenStream_.end());
    return b;
}

} // namespace rsvp
```

- [ ] **Step 6: Reimplement `serializeIndex` on `IndexBuilder`** — in `core/src/index.cpp`, replace the body of `serializeIndex` (keep its signature) with:
```cpp
#include "rsvp/indexbuilder.hpp"
// ...
std::vector<std::uint8_t> serializeIndex(const Document& doc, const DocMeta& meta,
                                         const std::vector<Chapter>& chapters,
                                         std::uint32_t seekInterval) {
    IndexBuilder ib(meta, seekInterval);
    std::size_t ci = 0;
    for (std::size_t i = 0; i < doc.tokens.size(); ++i) {
        while (ci < chapters.size() && chapters[ci].wordOffset == i) ib.startChapter(chapters[ci++].title);
        ib.addToken(doc.tokens[i].text, doc.tokens[i].flags);
    }
    while (ci < chapters.size()) ib.startChapter(chapters[ci++].title);
    return ib.finish();
}
```
(Remove the now-unused `putBlob16` from `index.cpp`'s anonymous namespace — it lives in `indexbuilder.cpp` now. Keep `kMagic`/`kVersion`, still used by `CompiledIndex::parse` / `indexMatchesSource`.)

- [ ] **Step 7: Run, expect green** — **[HOST-TEST]**: all PASS (new round-trip + byte-equality + existing `test_index` unchanged).

- [ ] **Step 8: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add core/include/rsvp/indexbuilder.hpp core/src/indexbuilder.cpp core/src/index.cpp test/host/test_indexbuilder.cpp test/host/CMakeLists.txt core/CMakeLists.txt; git commit -m "feat(core): incremental IndexBuilder; serializeIndex reimplemented on it"
```

---

## Task 2: Streaming tokenizer (`tokenizePlainTextInto`)

**Files:** Modify `core/include/rsvp/tokenize.hpp`, `core/src/tokenize.cpp`, `test/host/test_tokenize.cpp`.

- [ ] **Step 1: Header** — in `core/include/rsvp/tokenize.hpp` add `#include <functional>` and:
```cpp
// Streaming tokenizer: emits each word + flags to sink, in order, without building
// a Document. Same word/sentence/paragraph-flag semantics as tokenizePlainText
// (which is now a thin wrapper that collects into a Document).
void tokenizePlainTextInto(const std::string& text,
                           const std::function<void(const std::string&, std::uint8_t)>& sink);
```

- [ ] **Step 2: Failing test** — append to `test/host/test_tokenize.cpp`:
```cpp
TEST_CASE("tokenizePlainTextInto emits identical tokens to tokenizePlainText") {
    const std::string text = "Hello world.\n\nSecond para! Third \"quoted.\"\n\nEnd";
    Document viaDoc = tokenizePlainText(text);
    Document viaSink;
    tokenizePlainTextInto(text, [&](const std::string& w, std::uint8_t f) {
        viaSink.tokens.push_back(Token{w, f});
    });
    REQUIRE(viaSink.tokens.size() == viaDoc.tokens.size());
    for (std::size_t i = 0; i < viaDoc.tokens.size(); ++i) {
        CHECK(viaSink.tokens[i].text  == viaDoc.tokens[i].text);
        CHECK(viaSink.tokens[i].flags == viaDoc.tokens[i].flags);
    }
}
```

- [ ] **Step 3: Run, expect red** — **[HOST-TEST]**: FAILS (no `tokenizePlainTextInto`).

- [ ] **Step 4: Implement** — in `core/src/tokenize.cpp` add `#include <functional>`, then add `tokenizePlainTextInto` (one-token lookahead so a blank line can flag the *prior* word) and make `tokenizePlainText` a wrapper:
```cpp
void tokenizePlainTextInto(const std::string& text,
                           const std::function<void(const std::string&, std::uint8_t)>& sink) {
    const std::size_t n = text.size();
    std::size_t i = 0;
    std::string  prevWord; std::uint8_t prevFlags = FLAG_NONE; bool havePrev = false;
    bool pendingParagraphBreak = false;
    while (i < n) {
        bool sawNewline = false, blankLine = false;
        while (i < n && isSpace(static_cast<unsigned char>(text[i]))) {
            if (text[i] == '\n') { if (sawNewline) blankLine = true; sawNewline = true; }
            ++i;
        }
        if (blankLine && havePrev) pendingParagraphBreak = true;
        if (i >= n) break;
        const std::size_t start = i;
        while (i < n && !isSpace(static_cast<unsigned char>(text[i]))) ++i;
        std::string word = text.substr(start, i - start);
        if (pendingParagraphBreak && havePrev) { prevFlags |= FLAG_PARAGRAPH_END; pendingParagraphBreak = false; }
        if (havePrev) sink(prevWord, prevFlags);
        prevWord = std::move(word);
        prevFlags = endsSentence(prevWord) ? FLAG_SENTENCE_END : FLAG_NONE;
        havePrev = true;
    }
    if (havePrev) sink(prevWord, prevFlags);
}

Document tokenizePlainText(const std::string& text) {
    Document doc;
    tokenizePlainTextInto(text, [&](const std::string& w, std::uint8_t f) {
        doc.tokens.push_back(Token{w, f});
    });
    return doc;
}
```
Delete the old `tokenizePlainText` body (replaced by the wrapper above).

- [ ] **Step 5: Run, expect green** — **[HOST-TEST]**: all PASS (existing `test_tokenize` cases + the new equality case).

- [ ] **Step 6: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add core/include/rsvp/tokenize.hpp core/src/tokenize.cpp test/host/test_tokenize.cpp; git commit -m "feat(core): streaming tokenizePlainTextInto; tokenizePlainText wraps it"
```

---

## Task 3: Stream `epubToIndex` through `IndexBuilder`

**Files:** Modify `core/src/epub.cpp`.

- [ ] **Step 1: Refactor** — add `#include "rsvp/indexbuilder.hpp"` to `epub.cpp`, then replace the body of `epubToIndex` (from `Document doc;` through `return serializeIndex(...)`) so it never holds a full `Document`. The first emitted token of each 2nd+ non-empty chapter gets `FLAG_CHAPTER_START` (matching today's behavior):
```cpp
    DocMeta meta;
    meta.title       = opf.title;
    meta.author      = opf.author;
    meta.sourceSize  = sourceSize;
    meta.sourceMtime = sourceMtime;

    IndexBuilder ib(meta);
    bool anyTokens = false;                          // a previous chapter produced tokens
    for (const std::string& href : opf.spineHrefs) {
        std::string xhtml;
        if (!readZipEntry(epub, resolveHref(opfPath, href), xhtml)) continue;
        std::string text = htmlToText(xhtml);
        xhtml.clear(); xhtml.shrink_to_fit();
        const bool markChapterStart = anyTokens;     // not the very first non-empty chapter
        bool chapterHasTokens = false;
        bool firstOfChapter   = true;
        tokenizePlainTextInto(text, [&](const std::string& w, std::uint8_t f) {
            if (!chapterHasTokens) { ib.startChapter(); chapterHasTokens = true; anyTokens = true; }
            if (firstOfChapter && markChapterStart) f |= FLAG_CHAPTER_START;
            firstOfChapter = false;
            ib.addToken(w, f);
        });
    }
    if (!anyTokens) return {};
    return ib.finish();
```
(The old `Document doc; std::vector<Chapter> chapters;` locals and the `serializeIndex(doc, meta, chapters)` call are removed.)

- [ ] **Step 2: Run host tests** — **[HOST-TEST]**: existing `test_epub` PASSES (output equivalent — same tokens, chapter offsets, FLAG_CHAPTER_START on 2nd+ chapters). If a case fails, compare chapter-start flags/offsets and fix the per-chapter logic.

- [ ] **Step 3: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add core/src/epub.cpp; git commit -m "feat(core): stream epubToIndex through IndexBuilder (no full Document)"
```

---

## Task 4: `Player` reads from `CompiledIndex`

**Files:** Modify `core/include/rsvp/player.hpp`, `core/src/player.cpp`, `test/host/test_player.cpp`.

- [ ] **Step 1: Header** — in `core/include/rsvp/player.hpp` replace `#include "rsvp/token.hpp"` with `#include "rsvp/index.hpp"`, change the constructor + the private members:
```cpp
    Player(const CompiledIndex& idx, PacingConfig cfg);
    // ...
    const Token& current() const { return current_; }
    // ...
private:
    const CompiledIndex& idx_;
    PacingConfig         cfg_;
    Token                current_{};
    std::size_t          index_    = 0;
    int                  elapsed_  = 0;
    bool                 playing_  = false;
    bool                 finished_ = false;
```
Update `size()` to `return idx_.wordCount();`.

- [ ] **Step 2: Implement** — `core/src/player.cpp`, replace `doc_` usage with `idx_` + cached `current_`:
```cpp
Player::Player(const CompiledIndex& idx, PacingConfig cfg)
    : idx_(idx), cfg_(cfg), finished_(idx.wordCount() == 0) {
    if (idx_.wordCount() > 0) current_ = idx_.at(0);
}

void Player::play()       { if (idx_.wordCount() > 0 && !finished_) playing_ = true; }
void Player::pause()      { playing_ = false; }
void Player::togglePlay() { if (playing_) pause(); else play(); }

double Player::progress() const {
    if (idx_.wordCount() == 0) return 0.0;
    if (finished_)             return 1.0;
    return static_cast<double>(index_) / static_cast<double>(idx_.wordCount());
}

int Player::tick(int dtMs) {
    if (!playing_ || finished_ || idx_.wordCount() == 0 || dtMs <= 0) return 0;
    elapsed_ += dtMs;
    int advanced = 0;
    while (true) {
        int dur = wordDurationMs(current_, cfg_);
        if (dur < 1) dur = 1;
        if (elapsed_ < dur) break;
        elapsed_ -= dur;
        if (index_ + 1 >= idx_.wordCount()) { finished_ = true; playing_ = false; elapsed_ = 0; break; }
        ++index_; current_ = idx_.at(index_); ++advanced;
    }
    return advanced;
}

void Player::seek(std::size_t i) {
    if (idx_.wordCount() == 0) return;
    if (i >= idx_.wordCount()) i = idx_.wordCount() - 1;
    index_ = i; current_ = idx_.at(index_); elapsed_ = 0; finished_ = false;
}

void Player::nextSentence() {
    if (idx_.wordCount() == 0) return;
    std::size_t i = index_;
    for (; i < idx_.wordCount(); ++i) if (idx_.at(i).has(FLAG_SENTENCE_END)) break;
    if (i < idx_.wordCount()) seek(i + 1 < idx_.wordCount() ? i + 1 : idx_.wordCount() - 1);
    else                      seek(idx_.wordCount() - 1);
}

void Player::prevSentence() {
    if (idx_.wordCount() == 0) return;
    const std::size_t cur = index_;
    std::size_t s = 0;
    for (std::size_t i = cur; i-- > 0; ) if (idx_.at(i).has(FLAG_SENTENCE_END)) { s = i + 1; break; }
    if (s < cur) { seek(s); return; }
    if (s == 0)  { seek(0); return; }
    std::size_t ps = 0;
    for (std::size_t i = s - 1; i-- > 0; ) if (idx_.at(i).has(FLAG_SENTENCE_END)) { ps = i + 1; break; }
    seek(ps);
}
```

- [ ] **Step 3: Port `test_player`** — in `test/host/test_player.cpp`, add a helper to build a `CompiledIndex` from tokens and construct the `Player` from it. Add near the top (after includes):
```cpp
#include "rsvp/indexbuilder.hpp"
static rsvp::CompiledIndex makeIndex(const std::vector<rsvp::Token>& toks) {
    rsvp::IndexBuilder ib(rsvp::DocMeta{});
    for (const auto& t : toks) ib.addToken(t.text, t.flags);
    return rsvp::CompiledIndex::parse(ib.finish());
}
```
Then replace each `Document doc; doc.tokens = {...}; Player p(doc, cfg);` with
`auto idx = makeIndex({...}); Player p(idx, cfg);` (the `idx` must outlive `p` — keep it in the same scope). Assertions are unchanged.

- [ ] **Step 4: Run, expect green** — **[HOST-TEST]**: all PASS.

- [ ] **Step 5: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add core/include/rsvp/player.hpp core/src/player.cpp test/host/test_player.cpp; git commit -m "feat(core): Player reads on demand from CompiledIndex (no Document)"
```

---

## Task 5: book_loader + reader read from `CompiledIndex` (inline load)

**Files:** Modify `firmware/main/book_loader.hpp`, `firmware/main/book_loader.cpp`, `firmware/main/ui_reader.cpp`.

- [ ] **Step 1: `book_loader` returns a `CompiledIndex`** — `book_loader.hpp`:
```cpp
#pragma once
#include "rsvp/index.hpp"
#include <optional>
#include <string>

struct LoadedBook {
    rsvp::CompiledIndex index;
    std::string         title;
};

// Scan /sdcard for the first .epub/.txt, load-or-compile its index (cached in
// /sdcard/.rsvp/), parse it. nullopt on no-card/no-book/failure (caller substitutes
// the sample).
std::optional<LoadedBook> load_first_book();
```
In `book_loader.cpp`, change the final block to return the parsed index (drop `toDocument()`):
```cpp
    rsvp::CompiledIndex ci = rsvp::CompiledIndex::parse(idx);
    if (!ci.ok()) { ESP_LOGW(TAG, "index parse failed"); return std::nullopt; }
    std::string title = ci.meta().title;
    ESP_LOGI(TAG, "loaded \"%s\", %u words", title.c_str(), (unsigned)ci.wordCount());
    return LoadedBook{ std::move(ci), std::move(title) };
```

- [ ] **Step 2: Reader holds a `CompiledIndex`** — in `firmware/main/ui_reader.cpp`: replace the global `Document g_doc;` with `CompiledIndex g_index;` and add `#include "rsvp/indexbuilder.hpp"`. Replace the existing load-or-sample block inside `rsvp_reading_screen_create` (the `if (auto book = load_first_book()) { g_doc = … } else { g_doc = tokenizePlainText(…) }` + `static Player player(g_doc, cfg)`) with:
```cpp
    std::string book_title;
    if (auto book = load_first_book()) {
        g_index    = std::move(book->index);
        book_title = book->title;
    } else {
        rsvp::IndexBuilder ib(rsvp::DocMeta{});
        tokenizePlainTextInto(
            "Rapid serial visual presentation shows one word at a time. "
            "Your eyes stay still while the words flow past you. "
            "This little reader is now alive on the hardware!",
            [&](const std::string& w, std::uint8_t f){ ib.addToken(w, f); });
        g_index    = rsvp::CompiledIndex::parse(ib.finish());
        book_title = "Sample";
    }
    PacingConfig cfg; cfg.wpm = g_wpm;
    static Player player(g_index, cfg);
    g_player = &player;
    g_player->play();
```

- [ ] **Step 3: `refresh_word` reads from the index** — replace `g_doc.tokens[idx - 1]` / `g_doc.tokens[idx + 1]` / `g_doc.size()` / `g_doc.empty()` and `g_player->current().text`:
```cpp
void refresh_word()
{
    if (g_player == nullptr || g_index.wordCount() == 0) return;
    const std::size_t idx = g_player->index();

    const OrpSplit s = orpSplit(g_player->current().text);
    lv_label_set_text(g_pre,  s.pre.c_str());
    lv_label_set_text(g_orp,  s.orp.c_str());
    lv_label_set_text(g_post, s.post.c_str());

    lv_label_set_text(g_prev, idx > 0 ? g_index.at(idx - 1).text.c_str() : "");
    lv_label_set_text(g_next, (idx + 1 < g_index.wordCount()) ? g_index.at(idx + 1).text.c_str() : "");
    // ... rest unchanged (align ORP, ticks, update_status) ...
}
```
(`g_index.at(...)` returns a `Token` by value; bind to a local before `.text.c_str()` to avoid a dangling temporary:)
```cpp
    const Token prev = (idx > 0) ? g_index.at(idx - 1) : Token{};
    const Token next = (idx + 1 < g_index.wordCount()) ? g_index.at(idx + 1) : Token{};
    lv_label_set_text(g_prev, prev.text.c_str());
    lv_label_set_text(g_next, next.text.c_str());
```

- [ ] **Step 4: Build + flash + verify (inline load, no Loading screen yet)** — **[BUILD]**, **[FLASH]** with the PHM card in. **[CAPTURE 20]**. Expected: `SD mounted`, `book: …Project Hail Mary…`, `epubToIndex` completes (no crash/boot-loop), `loaded "Project Hail Mary", 149440 words`, then the device reads the real book (after a few-second blank while compiling). Reboot + **[CAPTURE 8]** → cache hit, near-instant. **This proves the memory fix end-to-end.**

- [ ] **Step 5: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add firmware/main/book_loader.hpp firmware/main/book_loader.cpp firmware/main/ui_reader.cpp; git commit -m "feat(firmware): read books on demand from CompiledIndex (no Document); big books load"
```

---

## Task 6: Loading screen + background compile task

**Files:** Modify `firmware/main/ui_reader.cpp`, `firmware/main/ui_reader.h`, `firmware/main/main.c`.

- [ ] **Step 1: Split screen creation + add the task/timer** — in `firmware/main/ui_reader.cpp`, add includes `#include "freertos/FreeRTOS.h"`, `#include "freertos/task.h"`, `#include <atomic>`. Add file-scope state and rename the reader builder:
```cpp
namespace {
std::atomic<bool> g_load_done{false};
std::string       g_loaded_title;
lv_obj_t*         g_loading_scr = nullptr;
lv_obj_t*         g_loading_lbl = nullptr;

void load_task(void*) {
    if (auto book = load_first_book()) { g_index = std::move(book->index); g_loaded_title = book->title; }
    else {
        rsvp::IndexBuilder ib(rsvp::DocMeta{});
        tokenizePlainTextInto(
            "Rapid serial visual presentation shows one word at a time. "
            "Your eyes stay still while the words flow past you. "
            "This little reader is now alive on the hardware!",
            [&](const std::string& w, std::uint8_t f){ ib.addToken(w, f); });
        g_index = rsvp::CompiledIndex::parse(ib.finish());
        g_loaded_title = "Sample";
    }
    g_load_done.store(true);
    vTaskDelete(nullptr);
}
} // namespace
```
Rename the existing `rsvp_reading_screen_create` body into `static void build_reader(const std::string& book_title)`, **removing** its internal load block (Task 5 Step 2) — `g_index` is already populated by `load_task`; it just builds labels + `Player player(g_index, cfg)` + gestures, using `book_title`.

- [ ] **Step 2: Loading screen + transition timer** — add:
```cpp
void loading_timer_cb(lv_timer_t* t) {
    if (!g_load_done.load()) return;
    if (g_loading_scr) { lv_obj_del(g_loading_scr); g_loading_scr = nullptr; }
    build_reader(g_loaded_title);
    lv_timer_del(t);
}

extern "C" void rsvp_loading_screen_create(void) {
    g_loading_scr = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(g_loading_scr);
    lv_obj_set_size(g_loading_scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_loading_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_loading_scr, LV_OPA_COVER, 0);
    g_loading_lbl = lv_label_create(g_loading_scr);
    lv_obj_set_style_text_color(g_loading_lbl, lv_color_hex(0xf2f5fa), 0);
    lv_obj_set_style_text_font(g_loading_lbl, &lv_font_montserrat_48, 0);
    lv_label_set_text(g_loading_lbl, "Loading...");
    lv_obj_center(g_loading_lbl);

    xTaskCreatePinnedToCore(load_task, "bookload", 32 * 1024, nullptr, 3, nullptr, 1);
    lv_timer_create(loading_timer_cb, 50, nullptr);
}
```

- [ ] **Step 3: Header + app_main** — in `firmware/main/ui_reader.h` rename the declaration to `void rsvp_loading_screen_create(void);`. In `firmware/main/main.c`, change the call inside the `example_lvgl_lock` block from `rsvp_reading_screen_create();` to `rsvp_loading_screen_create();`.

- [ ] **Step 4: Build + flash + verify UX** — **[BUILD]**, **[FLASH]** (card in). On-device: a **"Loading..."** screen appears immediately, then (after the compile) swaps to the reader streaming Project Hail Mary; gestures work. Reboot → cache hit, Loading flashes briefly then reads. Remove card → reads the sample. **[CAPTURE 20]** confirms `loaded … 149440 words` with no crash.

- [ ] **Step 5: Commit + update notes**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add firmware/main/ui_reader.cpp firmware/main/ui_reader.h firmware/main/main.c; git commit -m "feat(firmware): Loading screen + background compile task; swap to reader when ready"
```
Then update `docs/firmware-notes.md` — mark the big-book OOM resolved (streaming compile + on-demand reader + Loading screen); commit `docs: book loading is bounded-memory; full novels read`.

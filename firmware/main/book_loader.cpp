#include "book_loader.hpp"
#include "sdcard_bsp.h"
#include "rsvp/cachepath.hpp"
#include "rsvp/epub.hpp"
#include "rsvp/index.hpp"
#include "rsvp/indexbuilder.hpp"
#include "rsvp/tokenize.hpp"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

static const char *TAG = "book_loader";
// Source-file cap. Large std::vector allocations (>16KB) land in PSRAM automatically
// (CONFIG_SPIRAM_USE_MALLOC=y, ALWAYSINTERNAL=16384). 7MB is the ceiling for the 8MB
// PSRAM; read_file() additionally checks the actual free PSRAM before committing, so an
// over-budget book falls back gracefully instead of aborting (-fno-exceptions) mid-alloc.
static constexpr std::size_t kMaxBookBytes = 7u * 1024 * 1024;

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
    // The compile holds the raw bytes (contiguous) plus a decompress + index working set in
    // PSRAM. Verify the budget up front: with -fno-exceptions a failed vector alloc aborts.
    const std::size_t need = (std::size_t)st.st_size;
    if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < need ||
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < need + (2u << 20)) {
        ESP_LOGW(TAG, "book too large for free PSRAM (%u bytes)", (unsigned)need);
        return false;
    }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.resize(st.st_size);
    const std::size_t got = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size();
}

// Read just the first maxBytes of a file (enough for the index header during listing).
bool read_file_prefix(const std::string& path, std::vector<std::uint8_t>& out, std::size_t maxBytes) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.resize(maxBytes);
    const std::size_t got = fread(out.data(), 1, maxBytes, f);
    fclose(f);
    out.resize(got);
    return got > 0;
}

std::string filename_stem(const std::string& path) {
    std::size_t slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    std::size_t dot = name.find_last_of('.');
    return (dot == std::string::npos) ? name : name.substr(0, dot);
}

std::string pos_path(const std::string& bookPath) {
    return rsvp::cacheIndexPath(bookPath) + ".pos";   // /sdcard/.rsvp/<name>.idx.pos
}

rsvp::CompiledIndex compile_sample() {
    rsvp::IndexBuilder ib(rsvp::DocMeta{});
    rsvp::tokenizePlainTextInto(
        "Rapid serial visual presentation shows one word at a time. "
        "Your eyes stay still while the words flow past you. "
        "This little reader is now alive on the hardware!",
        [&](const std::string& w, std::uint8_t f){ ib.addToken(w, f); });
    return rsvp::CompiledIndex::parse(ib.finish());
}

} // namespace

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
    const std::size_t got = fread(&idx, 1, sizeof idx, f);
    fclose(f);
    return got == sizeof idx ? idx : 0;
}

std::optional<LoadedBook> load_book(const std::string& book) {
    if (book.empty()) {                       // built-in sample
        rsvp::CompiledIndex ci = compile_sample();
        if (!ci.ok()) return std::nullopt;
        return LoadedBook{ std::move(ci), "Sample" };
    }
    if (!sdcard_mounted()) { ESP_LOGW(TAG, "no SD card"); return std::nullopt; }

    struct stat st;
    if (stat(book.c_str(), &st) != 0) { ESP_LOGW(TAG, "stat failed: %s", book.c_str()); return std::nullopt; }
    const std::uint32_t size  = (std::uint32_t)st.st_size;
    const std::uint32_t mtime = (std::uint32_t)st.st_mtime;
    const std::string idxPath = rsvp::cacheIndexPath(book);

    std::vector<std::uint8_t> idx, cached;
    if (read_file(idxPath, cached) && rsvp::indexMatchesSource(cached, size, mtime)) {
        ESP_LOGI(TAG, "cache hit: %s", idxPath.c_str());
        idx = std::move(cached);
    } else {
        std::vector<std::uint8_t> raw;
        if (!read_file(book, raw)) { ESP_LOGW(TAG, "read failed"); return std::nullopt; }
        ESP_LOGI(TAG, "compiling %s (%u bytes)...", book.c_str(), (unsigned)raw.size());
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
            BookEntry be;
            be.path = "/sdcard/" + name;
            be.title = name;
            std::vector<std::uint8_t> head;
            if (read_file_prefix(rsvp::cacheIndexPath(be.path), head, 4096)) {
                rsvp::IndexHeader h = rsvp::readIndexHeader(head);
                if (h.ok) {
                    if (!h.title.empty()) be.title = h.title;
                    be.author = h.author;
                    be.wordCount = h.wordCount;
                }
            }
            be.position = load_position(be.path);
            out.push_back(std::move(be));
        }
        closedir(d);
    }
    out.push_back(BookEntry{ "", "Sample", "built-in", 0, 0 });   // always openable
    return out;
}

std::optional<LoadedBook> load_first_book() {
    return load_book(find_first_book());
}

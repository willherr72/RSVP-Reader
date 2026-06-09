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

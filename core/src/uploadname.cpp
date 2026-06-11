#include "rsvp/uploadname.hpp"
#include "rsvp/entity.hpp"
#include <cstring>
#include <cctype>
namespace rsvp {

static bool ends_with_ci(const std::string& s, const char* suf) {
    size_t n = s.size(), m = std::strlen(suf);
    if (n < m) return false;
    for (size_t i = 0; i < m; i++)
        if (std::tolower((unsigned char)s[n-m+i]) != std::tolower((unsigned char)suf[i])) return false;
    return true;
}

std::string sanitizeUploadName(const std::string& raw, bool forCreate) {
    size_t pos = raw.find_last_of("/\\");
    std::string name = (pos == std::string::npos) ? raw : raw.substr(pos + 1);
    if (name.empty() || name[0] == '.') return "";
    for (unsigned char c : name) if (c < 0x20) return "";          // control chars
    if (forCreate) {
        // FatFs (as configured) can't round-trip non-ASCII names: a smart quote in the
        // title makes the file unopenable after upload. Transliterate punctuation, then
        // flatten remaining non-ASCII code points and FAT-invalid chars to '_'.
        name = normalizeUnicodePunctuation(name);
        std::string ascii;
        ascii.reserve(name.size());
        for (size_t i = 0; i < name.size(); ) {
            unsigned char c = (unsigned char)name[i];
            if (c >= 0x80) {                                       // one '_' per code point
                ascii += '_';
                ++i;
                while (i < name.size() && ((unsigned char)name[i] & 0xC0) == 0x80) ++i;
            } else if (std::strchr("\\/:*?\"<>|", (char)c)) {      // FAT-invalid
                ascii += '_';
                ++i;
            } else {
                ascii += (char)c;
                ++i;
            }
        }
        name = std::move(ascii);
    }
    const bool epub = ends_with_ci(name, ".epub");
    const bool txt  = ends_with_ci(name, ".txt");
    if (!epub && !txt) return "";
    // Cap the stem so the derived cache path (".rsvp/<name>.idx.pos") stays well under
    // FatFs's 255-char filename limit; over-long titles otherwise fail to compile/load.
    const std::size_t extlen = epub ? 5 : 4;
    if (forCreate && name.size() - extlen > 120)          // truncate the stem, keep the extension
        name = name.substr(0, 120) + name.substr(name.size() - extlen);
    return name;
}

} // namespace rsvp

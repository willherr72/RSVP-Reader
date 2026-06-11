#include "rsvp/uploadname.hpp"
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

std::string sanitizeUploadName(const std::string& raw) {
    size_t pos = raw.find_last_of("/\\");
    std::string name = (pos == std::string::npos) ? raw : raw.substr(pos + 1);
    if (name.empty() || name[0] == '.') return "";
    for (unsigned char c : name) if (c < 0x20) return "";          // control chars
    if (!ends_with_ci(name, ".epub") && !ends_with_ci(name, ".txt")) return "";
    return name;
}

} // namespace rsvp

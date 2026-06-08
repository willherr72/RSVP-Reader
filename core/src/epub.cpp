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

#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace rsvp { namespace utf8 {

inline bool isContinuation(unsigned char c) { return (c & 0xC0u) == 0x80u; }

// Number of UTF-8 code points in s.
inline std::size_t length(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) if (!isContinuation(c)) ++n;
    return n;
}

// Byte offset where code point #cpIndex begins; s.size() if cpIndex >= length(s).
inline std::size_t byteOffset(const std::string& s, std::size_t cpIndex) {
    std::size_t cp = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (!isContinuation(static_cast<unsigned char>(s[i]))) {
            if (cp == cpIndex) return i;
            ++cp;
        }
    }
    return s.size();
}

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

}} // namespace rsvp::utf8

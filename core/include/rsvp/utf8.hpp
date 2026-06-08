#pragma once
#include <cstddef>
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

}} // namespace rsvp::utf8

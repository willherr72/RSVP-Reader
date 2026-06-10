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

std::string normalizeUnicodePunctuation(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    const std::size_t n = s.size();
    for (std::size_t i = 0; i < n; ) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xE2 && i + 2 < n && static_cast<unsigned char>(s[i + 1]) == 0x80) {
            switch (static_cast<unsigned char>(s[i + 2])) {
                case 0x98: case 0x99: out += '\'';  i += 3; continue;  // ' '  U+2018/2019
                case 0x9C: case 0x9D: out += '"';   i += 3; continue;  // " "  U+201C/201D
                case 0x93:            out += '-';   i += 3; continue;  // en dash U+2013
                case 0x94:            out += "--";  i += 3; continue;  // em dash U+2014
                case 0xA6:            out += "...";  i += 3; continue; // ellipsis U+2026
                case 0xA2:            out += '*';   i += 3; continue;  // bullet U+2022
                default: break;
            }
        } else if (c == 0xC2 && i + 1 < n && static_cast<unsigned char>(s[i + 1]) == 0xA0) {
            out += ' '; i += 2; continue;   // non-breaking space U+00A0
        }
        out += static_cast<char>(c); ++i;
    }
    return out;
}

} // namespace rsvp

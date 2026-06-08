#include "rsvp/orp.hpp"
#include "rsvp/utf8.hpp"

namespace rsvp {

std::size_t orpIndex(const std::string& word) {
    const std::size_t L = utf8::length(word);
    if (L == 0) return 0;
    std::size_t p;
    if      (L <= 3) p = 0;
    else if (L <= 5) p = 1;
    else if (L <= 9) p = 2;
    else             p = 3;
    if (p >= L) p = L - 1;   // clamp (defensive)
    return p;
}

OrpSplit orpSplit(const std::string& word) {
    const std::size_t p = orpIndex(word);
    const std::size_t a = utf8::byteOffset(word, p);
    const std::size_t b = utf8::byteOffset(word, p + 1);
    OrpSplit s;
    s.pre  = word.substr(0, a);
    s.orp  = word.substr(a, b - a);
    s.post = word.substr(b);
    return s;
}

} // namespace rsvp

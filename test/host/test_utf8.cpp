#include "doctest.h"
#include "rsvp/utf8.hpp"
using namespace rsvp;

TEST_CASE("utf8::appendCodePoint encodes 1-4 byte sequences") {
    std::string s;
    utf8::appendCodePoint(s, 0x41);     CHECK(s == "A");                  // 1 byte
    s.clear(); utf8::appendCodePoint(s, 0xE9);     CHECK(s == "\xC3\xA9");          // é, 2 bytes
    s.clear(); utf8::appendCodePoint(s, 0x2014);   CHECK(s == "\xE2\x80\x94");      // em dash, 3 bytes
    s.clear(); utf8::appendCodePoint(s, 0x1F600);  CHECK(s == "\xF0\x9F\x98\x80");  // emoji, 4 bytes
}

TEST_CASE("utf8::appendCodePoint output round-trips with utf8::length") {
    std::string s;
    utf8::appendCodePoint(s, 0x41);
    utf8::appendCodePoint(s, 0xE9);
    utf8::appendCodePoint(s, 0x2014);
    CHECK(utf8::length(s) == 3); // three code points regardless of byte count
}

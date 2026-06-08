#include "doctest.h"
#include "rsvp/byteio.hpp"
using namespace rsvp;

TEST_CASE("byteio: put/get round-trips u16 and u32") {
    std::vector<std::uint8_t> b;
    byteio::putU16(b, 0xABCD);
    byteio::putU32(b, 0x12345678u);
    std::size_t off = 0;
    CHECK(byteio::getU16(b, off) == 0xABCD);
    CHECK(off == 2);
    CHECK(byteio::getU32(b, off) == 0x12345678u);
    CHECK(off == 6);
}

TEST_CASE("byteio: writes little-endian byte order") {
    std::vector<std::uint8_t> b;
    byteio::putU16(b, 0x00FF);          // -> FF 00
    byteio::putU32(b, 0x000000FFu);     // -> FF 00 00 00
    CHECK(b[0] == 0xFF); CHECK(b[1] == 0x00);
    CHECK(b[2] == 0xFF); CHECK(b[3] == 0x00); CHECK(b[4] == 0x00); CHECK(b[5] == 0x00);
}

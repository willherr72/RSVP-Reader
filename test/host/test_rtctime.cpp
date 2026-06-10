#include "doctest.h"
#include "rsvp/rtctime.hpp"
using namespace rsvp;

TEST_CASE("bcd<->bin round-trips") {
    CHECK(bcdToBin(0x59) == 59);
    CHECK(bcdToBin(0x00) == 0);
    CHECK(binToBcd(59) == 0x59);
    CHECK(binToBcd(0) == 0x00);
    for (int n = 0; n < 60; ++n) CHECK(bcdToBin(binToBcd((std::uint8_t)n)) == n);
}

TEST_CASE("formatClock12h renders 12-hour time") {
    CHECK(formatClock12h(0, 0)   == "12:00 AM");   // midnight
    CHECK(formatClock12h(12, 0)  == "12:00 PM");   // noon
    CHECK(formatClock12h(13, 5)  == "1:05 PM");    // zero-padded minute
    CHECK(formatClock12h(14, 14) == "2:14 PM");
    CHECK(formatClock12h(9, 30)  == "9:30 AM");
    CHECK(formatClock12h(23, 59) == "11:59 PM");
}

TEST_CASE("to24h converts 12-hour + AM/PM") {
    CHECK(to24h(12, false) == 0);    // 12 AM
    CHECK(to24h(12, true)  == 12);   // 12 PM
    CHECK(to24h(1, false)  == 1);    // 1 AM
    CHECK(to24h(1, true)   == 13);   // 1 PM
    CHECK(to24h(11, true)  == 23);
}

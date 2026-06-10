#include "doctest.h"
#include "rsvp/battery.hpp"
using namespace rsvp;

TEST_CASE("batteryPercent clamps the ends") {
    CHECK(batteryPercent(3200) == 0);
    CHECK(batteryPercent(3300) == 0);
    CHECK(batteryPercent(4200) == 100);
    CHECK(batteryPercent(4300) == 100);
}

TEST_CASE("batteryPercent hits the curve points") {
    CHECK(batteryPercent(3700) == 25);
    CHECK(batteryPercent(3900) == 58);
    CHECK(batteryPercent(4000) == 76);
    CHECK(batteryPercent(3600) == 16);   // interpolated between 3500(8) and 3700(25)
}

TEST_CASE("batteryPercent is monotonic non-decreasing") {
    int prev = -1;
    for (int mv = 3200; mv <= 4300; mv += 25) {
        int p = batteryPercent(mv);
        CHECK(p >= prev);
        prev = p;
    }
}

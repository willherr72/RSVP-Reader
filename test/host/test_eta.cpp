#include "doctest.h"
#include "rsvp/eta.hpp"
using namespace rsvp;

TEST_CASE("etaString formats hours and minutes") {
    CHECK(etaString(40500, 300) == "2h 15m");
    CHECK(etaString(36000, 300) == "2h");      // exact hour -> no minutes
    CHECK(etaString(18000, 300) == "1h");
    CHECK(etaString(18300, 300) == "1h 1m");
    CHECK(etaString(13500, 300) == "45m");
}

TEST_CASE("etaString near-zero and invalid") {
    CHECK(etaString(100, 300) == "<1m");       // rounds to 0 minutes
    CHECK(etaString(0, 300) == "");            // nothing left
    CHECK(etaString(-5, 300) == "");
    CHECK(etaString(9000, 0) == "");           // no speed
}

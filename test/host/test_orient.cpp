#include "doctest.h"
#include "rsvp/orient.hpp"
using namespace rsvp;

TEST_CASE("orientFromAxis: beyond deadzone picks by sign") {
    CHECK(orientFromAxis(10000, 4000, ORIENT_FLIPPED) == ORIENT_NORMAL);
    CHECK(orientFromAxis(-10000, 4000, ORIENT_NORMAL) == ORIENT_FLIPPED);
    CHECK(orientFromAxis(4001, 4000, ORIENT_FLIPPED) == ORIENT_NORMAL);
    CHECK(orientFromAxis(-4001, 4000, ORIENT_NORMAL) == ORIENT_FLIPPED);
}

TEST_CASE("orientFromAxis: within deadzone keeps current") {
    CHECK(orientFromAxis(0, 4000, ORIENT_NORMAL) == ORIENT_NORMAL);
    CHECK(orientFromAxis(0, 4000, ORIENT_FLIPPED) == ORIENT_FLIPPED);
    CHECK(orientFromAxis(3999, 4000, ORIENT_FLIPPED) == ORIENT_FLIPPED);
    CHECK(orientFromAxis(-3999, 4000, ORIENT_NORMAL) == ORIENT_NORMAL);
}

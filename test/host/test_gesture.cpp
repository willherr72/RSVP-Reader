#include "doctest.h"
#include "rsvp/gesture.hpp"
using namespace rsvp;

TEST_CASE("classifyGesture: small movement is a tap") {
    CHECK(classifyGesture(0, 0, 20)    == Gesture::Tap);
    CHECK(classifyGesture(20, 0, 20)   == Gesture::Tap);   // on the radius
    CHECK(classifyGesture(-15, 19, 20) == Gesture::Tap);
}

TEST_CASE("classifyGesture: dominant horizontal axis -> left/right") {
    CHECK(classifyGesture(40, 5, 20)   == Gesture::SwipeRight);
    CHECK(classifyGesture(-40, -5, 20) == Gesture::SwipeLeft);
}

TEST_CASE("classifyGesture: dominant vertical axis -> up/down") {
    CHECK(classifyGesture(5, -40, 20)  == Gesture::SwipeUp);    // up = negative dy
    CHECK(classifyGesture(-5, 40, 20)  == Gesture::SwipeDown);
}

TEST_CASE("classifyGesture: axis tie resolves horizontal") {
    CHECK(classifyGesture(30, 30, 20)   == Gesture::SwipeRight);
    CHECK(classifyGesture(-30, -30, 20) == Gesture::SwipeLeft);
}

TEST_CASE("classifyGesture: just past the radius on one axis swipes") {
    CHECK(classifyGesture(21, 0, 20) == Gesture::SwipeRight);
    CHECK(classifyGesture(0, 21, 20) == Gesture::SwipeDown);
}

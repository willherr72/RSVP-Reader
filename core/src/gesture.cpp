#include "rsvp/gesture.hpp"
#include <cstdlib>

namespace rsvp {

Gesture classifyGesture(int dx, int dy, int tapRadius) {
    const int ax = std::abs(dx);
    const int ay = std::abs(dy);
    if (ax <= tapRadius && ay <= tapRadius) return Gesture::Tap;
    if (ax >= ay) return dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
    return dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
}

} // namespace rsvp

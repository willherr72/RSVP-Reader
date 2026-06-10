#include "rsvp/orient.hpp"
namespace rsvp {

ScreenOrient orientFromAxis(int axis, int deadzone, ScreenOrient current) {
    if (axis >  deadzone) return ORIENT_NORMAL;
    if (axis < -deadzone) return ORIENT_FLIPPED;
    return current;
}

} // namespace rsvp

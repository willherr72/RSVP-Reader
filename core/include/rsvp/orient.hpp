#pragma once
namespace rsvp {

enum ScreenOrient { ORIENT_NORMAL, ORIENT_FLIPPED };

// +axis beyond +deadzone -> NORMAL; below -deadzone -> FLIPPED; within deadzone -> keep `current`.
ScreenOrient orientFromAxis(int axis, int deadzone, ScreenOrient current);

} // namespace rsvp

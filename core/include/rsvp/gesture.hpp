#pragma once

namespace rsvp {

enum class Gesture { None, Tap, SwipeUp, SwipeDown, SwipeLeft, SwipeRight };

// dx,dy = release-minus-press in LVGL screen pixels (y grows downward).
// |delta| within tapRadius on both axes -> Tap; otherwise the dominant axis
// sets the swipe direction (ties resolve horizontal). Pure/deterministic.
// Always returns Tap or a Swipe*; None is reserved for the caller.
Gesture classifyGesture(int dx, int dy, int tapRadius);

} // namespace rsvp

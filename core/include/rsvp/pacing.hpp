#pragma once
#include "rsvp/token.hpp"

namespace rsvp {

struct PacingConfig {
    int    wpm                = 300;   // base words per minute
    double sentenceEndFactor  = 2.0;   // duration multiplier at sentence end
    double paragraphEndFactor = 2.5;   // at paragraph end
    double chapterStartFactor = 3.0;   // at chapter start
    int    longWordThreshold  = 8;     // chars beyond which the per-char bonus applies
    double longWordPerCharMs  = 0.0;   // ms added per char beyond the threshold
    int    minWordMs          = 60;    // clamp floor
};

// Base ms per word from WPM (wpm clamped to >= 1).
int baseWordMs(int wpm);

// Full display duration for a token given its flags + length.
int wordDurationMs(const Token& tok, const PacingConfig& cfg);

} // namespace rsvp

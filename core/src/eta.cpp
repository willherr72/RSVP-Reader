#include "rsvp/eta.hpp"
namespace rsvp {

std::string etaString(int remainingWords, int wpm) {
    if (wpm <= 0 || remainingWords <= 0) return "";
    int mins = (remainingWords + wpm / 2) / wpm;          // round to nearest minute
    if (mins < 1)  return "<1m";
    if (mins < 60) return std::to_string(mins) + "m";
    int h = mins / 60, m = mins % 60;
    if (m == 0)    return std::to_string(h) + "h";
    return std::to_string(h) + "h " + std::to_string(m) + "m";
}

} // namespace rsvp

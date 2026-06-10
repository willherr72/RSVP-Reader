#pragma once
#include <string>
namespace rsvp {

std::string etaString(int remainingWords, int wpm);   // "2h 15m" / "45m" / "<1m" / "" (done/invalid)

} // namespace rsvp

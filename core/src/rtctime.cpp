#include "rsvp/rtctime.hpp"
#include <cstdio>

namespace rsvp {

std::uint8_t bcdToBin(std::uint8_t bcd) { return (std::uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F)); }
std::uint8_t binToBcd(std::uint8_t bin) { return (std::uint8_t)(((bin / 10) << 4) | (bin % 10)); }

std::string formatClock12h(std::uint8_t hour24, std::uint8_t minute) {
    int h = hour24 % 24;
    const char* ap = (h < 12) ? "AM" : "PM";
    int h12 = h % 12; if (h12 == 0) h12 = 12;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d %s", h12, minute % 60, ap);
    return buf;
}

std::uint8_t to24h(std::uint8_t hour12, bool isPM) {
    int h = hour12 % 12;       // 12 -> 0
    if (isPM) h += 12;
    return (std::uint8_t)h;
}

} // namespace rsvp

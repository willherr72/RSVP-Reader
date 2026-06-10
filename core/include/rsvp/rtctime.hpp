#pragma once
#include <cstdint>
#include <string>

namespace rsvp {

struct RtcTime { std::uint8_t hour; std::uint8_t minute; std::uint8_t second; };  // hour 0..23

std::uint8_t bcdToBin(std::uint8_t bcd);
std::uint8_t binToBcd(std::uint8_t bin);
std::string  formatClock12h(std::uint8_t hour24, std::uint8_t minute);   // "2:14 PM", "12:05 AM"
std::uint8_t to24h(std::uint8_t hour12, bool isPM);                       // 12 AM->0, 12 PM->12, 1 PM->13

} // namespace rsvp

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace rsvp { namespace byteio {

// Append little-endian integers to a byte buffer.
inline void putU16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
inline void putU32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

// Read little-endian integers starting at off, advancing off. Caller bounds-checks.
inline std::uint16_t getU16(const std::vector<std::uint8_t>& b, std::size_t& off) {
    const std::uint16_t v = static_cast<std::uint16_t>(b[off])
                          | (static_cast<std::uint16_t>(b[off + 1]) << 8);
    off += 2;
    return v;
}
inline std::uint32_t getU32(const std::vector<std::uint8_t>& b, std::size_t& off) {
    const std::uint32_t v = static_cast<std::uint32_t>(b[off])
                          | (static_cast<std::uint32_t>(b[off + 1]) << 8)
                          | (static_cast<std::uint32_t>(b[off + 2]) << 16)
                          | (static_cast<std::uint32_t>(b[off + 3]) << 24);
    off += 4;
    return v;
}

}} // namespace rsvp::byteio

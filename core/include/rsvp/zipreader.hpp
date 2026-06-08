#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rsvp {

// Extract entry `name` from an in-memory ZIP archive into `out` (raw bytes).
// Returns false if `zip` is not a valid ZIP or the entry is missing. `out` is
// always cleared first.
bool readZipEntry(const std::vector<std::uint8_t>& zip, const std::string& name, std::string& out);

} // namespace rsvp

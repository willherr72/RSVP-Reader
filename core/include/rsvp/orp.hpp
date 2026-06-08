#pragma once
#include <cstddef>
#include <string>

namespace rsvp {

// 0-based code-point index of the Optimal Recognition Point letter.
std::size_t orpIndex(const std::string& word);

// The word cut into the part before the ORP letter, the ORP letter, and the rest.
struct OrpSplit { std::string pre, orp, post; };
OrpSplit orpSplit(const std::string& word);

} // namespace rsvp

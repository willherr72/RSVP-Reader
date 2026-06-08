#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rsvp {

// Resolve a content href against the OPF file's ZIP-internal path, normalizing
// "./" and "../". e.g. resolveHref("OEBPS/content.opf", "chap1.xhtml") == "OEBPS/chap1.xhtml".
std::string resolveHref(const std::string& opfPath, const std::string& href);

// Build a compiled index (Plan 2 format) from EPUB (ZIP) bytes. One chapter per spine
// document (2nd+ chapters' first token carries FLAG_CHAPTER_START). Returns empty on
// failure (not a ZIP / no container / no OPF / no readable spine text).
std::vector<std::uint8_t> epubToIndex(const std::vector<std::uint8_t>& epub,
                                      std::uint32_t sourceSize, std::uint32_t sourceMtime);

} // namespace rsvp

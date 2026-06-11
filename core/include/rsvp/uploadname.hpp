#pragma once
#include <string>
namespace rsvp {

// Safe /sdcard basename, or "" if invalid. forCreate makes the name safe to CREATE on FatFs:
// caps over-long stems (cache paths must fit FatFs limits), transliterates Unicode punctuation,
// replaces other non-ASCII and FAT-invalid chars — non-ASCII names don't round-trip through
// FatFs and become unopenable. Pass false when referencing an EXISTING file (delete), where
// the name must match the on-disk one exactly.
std::string sanitizeUploadName(const std::string& raw, bool forCreate = true);

} // namespace rsvp

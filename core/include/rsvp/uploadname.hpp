#pragma once
#include <string>
namespace rsvp {

std::string sanitizeUploadName(const std::string& raw);   // safe /sdcard basename, or "" if invalid

} // namespace rsvp

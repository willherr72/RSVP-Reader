#pragma once
#include <string>

namespace rsvp {

// Map a book path to its compiled-index cache path in a sibling ".rsvp" dir,
// keeping the full filename (incl. extension) so Foo.epub and Foo.txt don't
// collide. e.g. "/sdcard/Foo.epub" -> "/sdcard/.rsvp/Foo.epub.idx". Deterministic.
std::string cacheIndexPath(const std::string& bookPath);

} // namespace rsvp

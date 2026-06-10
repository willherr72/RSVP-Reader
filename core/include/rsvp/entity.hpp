#pragma once
#include <string>

namespace rsvp {

// Decode HTML/XML character entities in s to UTF-8: &amp; &lt; &gt; &quot; &apos;,
// numeric &#NN; / &#xHH;, and common named punctuation. &nbsp; becomes a regular
// space. Unknown or malformed entities are left literal.
std::string decodeEntities(const std::string& s);

// Map common Unicode punctuation to ASCII so the ASCII-only reader font can render it:
// curly quotes ' ' " " -> ' ", en/em dashes -> - / --, ellipsis -> ..., bullet -> *,
// non-breaking space -> space. All other bytes pass through unchanged.
std::string normalizeUnicodePunctuation(const std::string& s);

} // namespace rsvp

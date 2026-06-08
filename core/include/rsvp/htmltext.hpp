#pragma once
#include <string>

namespace rsvp {

// Convert XHTML/HTML to plain reading text: tags removed, entities decoded,
// <script>/<style> content dropped, block elements separated by blank lines
// (paragraphs), inline tags and <br> reduced to a space, whitespace normalized.
// The output is suitable input for tokenizePlainText.
std::string htmlToText(const std::string& html);

} // namespace rsvp

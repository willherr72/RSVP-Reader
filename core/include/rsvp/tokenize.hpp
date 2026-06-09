#pragma once
#include "rsvp/token.hpp"
#include <functional>
#include <string>

namespace rsvp {

// Split plain UTF-8 text into a Document. Words are whitespace-delimited (punctuation
// stays attached). FLAG_SENTENCE_END is set on words ending a sentence; FLAG_PARAGRAPH_END
// on the last word before a blank line. (Plain text has no chapters -> no FLAG_CHAPTER_START.)
Document tokenizePlainText(const std::string& text);

// Streaming variant: emits each word + flags to sink, in order, without building a
// Document. Same semantics as tokenizePlainText (which is now a thin wrapper over it).
void tokenizePlainTextInto(const std::string& text,
                           const std::function<void(const std::string&, std::uint8_t)>& sink);

} // namespace rsvp

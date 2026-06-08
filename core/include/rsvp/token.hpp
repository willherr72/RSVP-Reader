#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rsvp {

// Structural flags attached to a token; consumed by the pacing engine.
enum TokenFlags : std::uint8_t {
    FLAG_NONE          = 0,
    FLAG_SENTENCE_END  = 1u << 0,  // token ends a sentence (. ! ?)
    FLAG_PARAGRAPH_END = 1u << 1,  // token ends a paragraph
    FLAG_CHAPTER_START = 1u << 2,  // token starts a chapter
};

struct Token {
    std::string  text;            // the word as displayed (UTF-8)
    std::uint8_t flags = FLAG_NONE;
    bool has(TokenFlags f) const {
        return (flags & static_cast<std::uint8_t>(f)) != 0;
    }
};

// In-memory document: an ordered list of tokens.
struct Document {
    std::vector<Token> tokens;
    std::size_t size()  const { return tokens.size(); }
    bool        empty() const { return tokens.empty(); }
};

} // namespace rsvp

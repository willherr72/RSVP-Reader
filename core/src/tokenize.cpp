#include "rsvp/tokenize.hpp"

namespace rsvp {

namespace {

bool isSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// A word ends a sentence if, after stripping trailing closing quotes/brackets,
// it ends with '.', '!' or '?'.
bool endsSentence(const std::string& w) {
    std::size_t e = w.size();
    auto isCloser = [](char c) {
        return c == '"' || c == '\'' || c == ')' || c == ']' || c == '}';
    };
    while (e > 0 && isCloser(w[e - 1])) --e;
    if (e == 0) return false;
    const char c = w[e - 1];
    return c == '.' || c == '!' || c == '?';
}

} // namespace

Document tokenizePlainText(const std::string& text) {
    Document doc;
    const std::size_t n = text.size();
    std::size_t i = 0;
    bool pendingParagraphBreak = false;  // a blank line was seen after the last emitted word
    while (i < n) {
        // Skip whitespace; a gap containing >= 2 newlines is a blank line (paragraph break).
        bool sawNewline = false, blankLine = false;
        while (i < n && isSpace(static_cast<unsigned char>(text[i]))) {
            if (text[i] == '\n') { if (sawNewline) blankLine = true; sawNewline = true; }
            ++i;
        }
        if (blankLine && !doc.tokens.empty()) pendingParagraphBreak = true;
        if (i >= n) break;   // trailing whitespace at EOF: a pending break has no next word -> dropped
        // Read one word (maximal run of non-whitespace).
        const std::size_t start = i;
        while (i < n && !isSpace(static_cast<unsigned char>(text[i]))) ++i;
        std::string word = text.substr(start, i - start);
        // A blank line preceded this word -> the previous word ended its paragraph.
        if (pendingParagraphBreak && !doc.tokens.empty()) {
            doc.tokens.back().flags |= FLAG_PARAGRAPH_END;
            pendingParagraphBreak = false;
        }
        std::uint8_t flags = FLAG_NONE;
        if (endsSentence(word)) flags |= FLAG_SENTENCE_END;
        doc.tokens.push_back(Token{word, flags});
    }
    return doc;
}

} // namespace rsvp

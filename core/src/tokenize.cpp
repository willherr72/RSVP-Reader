#include "rsvp/tokenize.hpp"
#include "rsvp/entity.hpp"
#include <functional>
#include <utility>

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

void tokenizePlainTextInto(const std::string& rawText,
                           const std::function<void(const std::string&, std::uint8_t)>& sink) {
    const std::string text = normalizeUnicodePunctuation(rawText);   // smart quotes/dashes -> ASCII
    const std::size_t n = text.size();
    std::size_t i = 0;
    // One-token lookahead: a blank line flags FLAG_PARAGRAPH_END on the PRIOR word, so we
    // hold the previous word until we know whether a blank line followed it.
    std::string  prevWord;
    std::uint8_t prevFlags = FLAG_NONE;
    bool         havePrev = false;
    bool pendingParagraphBreak = false;
    while (i < n) {
        // Skip whitespace; a gap containing >= 2 newlines is a blank line (paragraph break).
        bool sawNewline = false, blankLine = false;
        while (i < n && isSpace(static_cast<unsigned char>(text[i]))) {
            if (text[i] == '\n') { if (sawNewline) blankLine = true; sawNewline = true; }
            ++i;
        }
        if (blankLine && havePrev) pendingParagraphBreak = true;
        if (i >= n) break;   // trailing whitespace at EOF: a pending break has no next word -> dropped
        // Read one word (maximal run of non-whitespace).
        const std::size_t start = i;
        while (i < n && !isSpace(static_cast<unsigned char>(text[i]))) ++i;
        std::string word = text.substr(start, i - start);
        // A blank line preceded this word -> the previous word ended its paragraph.
        if (pendingParagraphBreak && havePrev) { prevFlags |= FLAG_PARAGRAPH_END; pendingParagraphBreak = false; }
        if (havePrev) sink(prevWord, prevFlags);
        prevWord  = std::move(word);
        prevFlags = endsSentence(prevWord) ? FLAG_SENTENCE_END : FLAG_NONE;
        havePrev  = true;
    }
    if (havePrev) sink(prevWord, prevFlags);
}

Document tokenizePlainText(const std::string& text) {
    Document doc;
    tokenizePlainTextInto(text, [&](const std::string& w, std::uint8_t f) {
        doc.tokens.push_back(Token{w, f});
    });
    return doc;
}

} // namespace rsvp

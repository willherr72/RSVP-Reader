#include "rsvp/htmltext.hpp"
#include "rsvp/entity.hpp"

namespace rsvp {
namespace {

bool isHSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool isBlockTag(const std::string& name) {
    static const char* blocks[] = {
        "p","div","h1","h2","h3","h4","h5","h6","li","ul","ol","blockquote",
        "section","article","header","footer","figure","figcaption","pre","hr",
        "table","tr","td","th","thead","tbody","body","main","aside","nav",
        "dd","dt","dl","address","fieldset","form","caption","colgroup"
    };
    for (const char* b : blocks) if (name == b) return true;
    return false;
}

// Parse a tag's lowercased name from the text between '<' and '>'. Sets isEnd for a
// closing tag. Returns "" for comments / doctype / processing instructions.
std::string tagName(const std::string& inner, bool& isEnd) {
    isEnd = false;
    std::size_t i = 0;
    const std::size_t n = inner.size();
    while (i < n && isHSpace(static_cast<unsigned char>(inner[i]))) ++i;
    if (i < n && inner[i] == '/') { isEnd = true; ++i; }
    if (i < n && (inner[i] == '!' || inner[i] == '?')) return "";
    std::string name;
    while (i < n) {
        const char c = inner[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            name.push_back(lower(c)); ++i;
        } else break;
    }
    return name;
}

// Case-insensitive search for needle in hay starting at from.
std::size_t ifind(const std::string& hay, const std::string& needle, std::size_t from) {
    if (needle.empty() || from > hay.size()) return std::string::npos;
    for (std::size_t i = from; i + needle.size() <= hay.size(); ++i) {
        std::size_t j = 0;
        for (; j < needle.size(); ++j) if (lower(hay[i + j]) != lower(needle[j])) break;
        if (j == needle.size()) return i;
    }
    return std::string::npos;
}

// Collapse whitespace: runs without a newline -> a single space; runs containing a
// newline -> a paragraph break ("\n\n"). Leading/trailing whitespace trimmed.
std::string normalizeWhitespace(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '\n' || isHSpace(c)) {
            bool hasNewline = false;
            while (i < n && (s[i] == '\n' || isHSpace(static_cast<unsigned char>(s[i])))) {
                if (s[i] == '\n') hasNewline = true;
                ++i;
            }
            if (!out.empty()) out += hasNewline ? "\n\n" : " ";
        } else {
            out.push_back(static_cast<char>(c));
            ++i;
        }
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

} // namespace

std::string htmlToText(const std::string& html) {
    // Phase 1: strip tags into `raw`. Block boundaries become '\n', <br> and source
    // whitespace become ' ', script/style content is dropped, inline tags vanish.
    std::string raw;
    raw.reserve(html.size());
    const std::size_t n = html.size();
    std::size_t i = 0;
    while (i < n) {
        const char ch = html[i];
        if (ch == '<') {
            const std::size_t end = html.find('>', i + 1);
            if (end == std::string::npos) break; // unterminated tag: drop the rest
            const std::string inner = html.substr(i + 1, end - i - 1);
            bool isEnd = false;
            const std::string name = tagName(inner, isEnd);
            if (!isEnd && (name == "script" || name == "style")) {
                const std::string close = "</" + name + ">";
                const std::size_t cpos = ifind(html, close, end + 1);
                i = (cpos == std::string::npos) ? n : cpos + close.size();
                continue;
            }
            if (name == "br") raw.push_back(' ');
            else if (isBlockTag(name)) raw.push_back('\n');
            // inline tags: removed
            i = end + 1;
            continue;
        }
        if (ch == '\n' || isHSpace(static_cast<unsigned char>(ch))) raw.push_back(' ');
        else raw.push_back(ch);
        ++i;
    }
    // Phase 2: decode entities, then normalize whitespace into paragraphs.
    return normalizeWhitespace(decodeEntities(raw));
}

} // namespace rsvp

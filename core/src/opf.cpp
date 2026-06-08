#include "rsvp/opf.hpp"

namespace rsvp {
namespace {
bool isWs(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }
} // namespace

std::string attrValue(const std::string& s, const std::string& name) {
    const std::size_t n = s.size(), m = name.size();
    if (m == 0) return "";
    std::size_t i = 0;
    while (i < n) {
        const std::size_t pos = s.find(name, i);
        if (pos == std::string::npos) return "";
        const bool leftOk = (pos == 0) || isWs(s[pos - 1]);
        std::size_t j = pos + m;
        while (j < n && isWs(s[j])) ++j;
        if (leftOk && j < n && s[j] == '=') {
            ++j;
            while (j < n && isWs(s[j])) ++j;
            if (j < n && (s[j] == '"' || s[j] == '\'')) {
                const char q = s[j++];
                const std::size_t start = j;
                while (j < n && s[j] != q) ++j;
                return s.substr(start, j - start);
            }
        }
        i = pos + 1;
    }
    return "";
}

// --- stubs replaced in Tasks 1 and 2 ---
std::string parseContainerOpfPath(const std::string&) { return ""; }
OpfData parseOpf(const std::string&) { return OpfData{}; }

} // namespace rsvp

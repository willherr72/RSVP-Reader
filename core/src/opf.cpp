#include "rsvp/opf.hpp"
#include "rsvp/entity.hpp"
#include <vector>

namespace rsvp {
namespace {
bool isWs(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }

// Local tag name (namespace prefix stripped, lowercased) from a tag's inner text.
std::string readLocalTagName(const std::string& inner, bool& isEnd, bool& selfClose) {
    isEnd = false;
    selfClose = !inner.empty() && inner.back() == '/';
    std::size_t i = 0;
    const std::size_t n = inner.size();
    while (i < n && isWs(inner[i])) ++i;
    if (i < n && inner[i] == '/') { isEnd = true; ++i; }
    if (i < n && (inner[i] == '!' || inner[i] == '?')) return "";
    std::string name;
    while (i < n) {
        const char c = inner[i];
        if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==':'||c=='-'||c=='_') {
            name.push_back(c); ++i;
        } else break;
    }
    const std::size_t colon = name.find(':');
    if (colon != std::string::npos) name = name.substr(colon + 1);
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return name;
}

// The inner text (name + attributes) of every non-end tag whose local name == local.
std::vector<std::string> tagInners(const std::string& xml, const std::string& local) {
    std::vector<std::string> out;
    std::size_t i = 0;
    const std::size_t n = xml.size();
    while (i < n) {
        const std::size_t lt = xml.find('<', i); if (lt == std::string::npos) break;
        const std::size_t gt = xml.find('>', lt + 1); if (gt == std::string::npos) break;
        const std::string inner = xml.substr(lt + 1, gt - lt - 1);
        bool isEnd = false, selfClose = false;
        if (readLocalTagName(inner, isEnd, selfClose) == local && !isEnd) out.push_back(inner);
        i = gt + 1;
    }
    return out;
}

// Trimmed, entity-decoded text of the first non-end, non-self-closing element with local name == local.
std::string elementText(const std::string& xml, const std::string& local) {
    std::size_t i = 0;
    const std::size_t n = xml.size();
    while (i < n) {
        const std::size_t lt = xml.find('<', i); if (lt == std::string::npos) return "";
        const std::size_t gt = xml.find('>', lt + 1); if (gt == std::string::npos) return "";
        const std::string inner = xml.substr(lt + 1, gt - lt - 1);
        bool isEnd = false, selfClose = false;
        if (readLocalTagName(inner, isEnd, selfClose) == local && !isEnd && !selfClose) {
            const std::size_t te = xml.find('<', gt + 1);
            const std::string text = xml.substr(gt + 1, (te == std::string::npos ? n : te) - (gt + 1));
            std::size_t a = 0, b = text.size();
            while (a < b && isWs(text[a])) ++a;
            while (b > a && isWs(text[b - 1])) --b;
            return decodeEntities(text.substr(a, b - a));
        }
        i = gt + 1;
    }
    return "";
}

bool containsStr(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}
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
std::string parseContainerOpfPath(const std::string& xml) {
    const std::size_t pos = xml.find("<rootfile ");
    if (pos == std::string::npos) return "";
    const std::size_t end = xml.find('>', pos);
    if (end == std::string::npos) return "";
    return attrValue(xml.substr(pos, end - pos), "full-path");
}
OpfData parseOpf(const std::string& xml) {
    OpfData d;
    d.title  = elementText(xml, "title");
    d.author = elementText(xml, "creator");

    // Manifest: collect id/href pairs; note an EPUB3 cover-image href.
    std::vector<std::string> ids, hrefs;
    std::string coverFromProps;
    for (const std::string& it : tagInners(xml, "item")) {
        const std::string id = attrValue(it, "id");
        const std::string href = attrValue(it, "href");
        if (id.empty() || href.empty()) continue;
        ids.push_back(id);
        hrefs.push_back(href);
        if (containsStr(attrValue(it, "properties"), "cover-image")) coverFromProps = href;
    }
    auto hrefForId = [&](const std::string& id) -> std::string {
        for (std::size_t k = 0; k < ids.size(); ++k) if (ids[k] == id) return hrefs[k];
        return "";
    };

    // EPUB2 cover fallback: <meta name="cover" content="ID">.
    std::string coverFromMeta;
    for (const std::string& mt : tagInners(xml, "meta")) {
        if (attrValue(mt, "name") == "cover") { coverFromMeta = hrefForId(attrValue(mt, "content")); break; }
    }

    // Spine: resolve each itemref's idref to its manifest href, in order.
    for (const std::string& ir : tagInners(xml, "itemref")) {
        const std::string href = hrefForId(attrValue(ir, "idref"));
        if (!href.empty()) d.spineHrefs.push_back(href);
    }

    d.coverHref = !coverFromProps.empty() ? coverFromProps : coverFromMeta;
    return d;
}

} // namespace rsvp

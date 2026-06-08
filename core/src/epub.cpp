#include "rsvp/epub.hpp"
#include "rsvp/zipreader.hpp"
#include "rsvp/opf.hpp"
#include "rsvp/htmltext.hpp"
#include "rsvp/tokenize.hpp"
#include "rsvp/index.hpp"
#include <string>
#include <vector>

namespace rsvp {

std::string resolveHref(const std::string& opfPath, const std::string& href) {
    const std::size_t slash = opfPath.find_last_of('/');
    const std::string base = (slash == std::string::npos) ? std::string() : opfPath.substr(0, slash + 1);
    const std::string path = base + href;

    std::vector<std::string> parts;
    std::string seg;
    auto flush = [&]() {
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!seg.empty() && seg != ".") parts.push_back(seg);
        seg.clear();
    };
    for (const char c : path) {
        if (c == '/') flush();
        else seg.push_back(c);
    }
    flush();

    std::string out;
    for (std::size_t k = 0; k < parts.size(); ++k) {
        if (k) out.push_back('/');
        out += parts[k];
    }
    return out;
}

std::vector<std::uint8_t> epubToIndex(const std::vector<std::uint8_t>& epub,
                                      std::uint32_t sourceSize, std::uint32_t sourceMtime) {
    std::string container;
    if (!readZipEntry(epub, "META-INF/container.xml", container)) return {};
    const std::string opfPath = parseContainerOpfPath(container);
    if (opfPath.empty()) return {};
    std::string opfXml;
    if (!readZipEntry(epub, opfPath, opfXml)) return {};
    const OpfData opf = parseOpf(opfXml);

    Document doc;
    std::vector<Chapter> chapters;
    for (const std::string& href : opf.spineHrefs) {
        std::string xhtml;
        if (!readZipEntry(epub, resolveHref(opfPath, href), xhtml)) continue;
        Document part = tokenizePlainText(htmlToText(xhtml));
        if (part.tokens.empty()) continue;
        if (!doc.tokens.empty()) part.tokens.front().flags |= FLAG_CHAPTER_START;
        chapters.push_back(Chapter{static_cast<std::uint32_t>(doc.tokens.size()), std::string()});
        for (Token& t : part.tokens) doc.tokens.push_back(std::move(t));
    }
    if (doc.tokens.empty()) return {};

    DocMeta meta;
    meta.title       = opf.title;
    meta.author      = opf.author;
    meta.sourceSize  = sourceSize;
    meta.sourceMtime = sourceMtime;
    return serializeIndex(doc, meta, chapters);
}

} // namespace rsvp

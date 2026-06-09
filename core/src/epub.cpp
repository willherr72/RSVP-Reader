#include "rsvp/epub.hpp"
#include "rsvp/zipreader.hpp"
#include "rsvp/opf.hpp"
#include "rsvp/htmltext.hpp"
#include "rsvp/tokenize.hpp"
#include "rsvp/index.hpp"
#include "rsvp/indexbuilder.hpp"
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

    DocMeta meta;
    meta.title       = opf.title;
    meta.author      = opf.author;
    meta.sourceSize  = sourceSize;
    meta.sourceMtime = sourceMtime;

    IndexBuilder ib(meta);
    bool anyTokens = false;                          // a previous chapter produced tokens
    for (const std::string& href : opf.spineHrefs) {
        std::string xhtml;
        if (!readZipEntry(epub, resolveHref(opfPath, href), xhtml)) continue;
        std::string text = htmlToText(xhtml);
        xhtml.clear(); xhtml.shrink_to_fit();
        const bool markChapterStart = anyTokens;     // not the very first non-empty chapter
        bool chapterHasTokens = false;
        bool firstOfChapter   = true;
        tokenizePlainTextInto(text, [&](const std::string& w, std::uint8_t f) {
            if (!chapterHasTokens) { ib.startChapter(); chapterHasTokens = true; anyTokens = true; }
            if (firstOfChapter && markChapterStart) f |= FLAG_CHAPTER_START;
            firstOfChapter = false;
            ib.addToken(w, f);
        });
    }
    if (!anyTokens) return {};
    return ib.finish();
}

} // namespace rsvp

#include "doctest.h"
#include "rsvp/epub.hpp"
#include "rsvp/index.hpp"
#include "miniz.h"
#include <string>
#include <utility>
#include <vector>
using namespace rsvp;

TEST_CASE("resolveHref: resolves spine hrefs against the OPF directory") {
    CHECK(resolveHref("OEBPS/content.opf", "chap1.xhtml") == "OEBPS/chap1.xhtml");
    CHECK(resolveHref("content.opf", "a.xhtml") == "a.xhtml");
    CHECK(resolveHref("OEBPS/text/content.opf", "../img/c.png") == "OEBPS/img/c.png");
    CHECK(resolveHref("OEBPS/content.opf", "sub/d.xhtml") == "OEBPS/sub/d.xhtml");
    CHECK(resolveHref("OEBPS/content.opf", "./x.xhtml") == "OEBPS/x.xhtml");
}

// Build an in-memory ZIP (EPUB) from {name, content} entries.
static std::vector<std::uint8_t> makeEpub(std::initializer_list<std::pair<std::string, std::string>> entries) {
    mz_zip_archive z;
    mz_zip_zero_struct(&z);
    mz_zip_writer_init_heap(&z, 0, 0);
    for (const auto& e : entries)
        mz_zip_writer_add_mem(&z, e.first.c_str(), e.second.data(), e.second.size(), MZ_BEST_COMPRESSION);
    void* buf = nullptr;
    size_t sz = 0;
    mz_zip_writer_finalize_heap_archive(&z, &buf, &sz);
    std::vector<std::uint8_t> out(static_cast<std::uint8_t*>(buf), static_cast<std::uint8_t*>(buf) + sz);
    mz_zip_writer_end(&z);
    mz_free(buf);
    return out;
}

TEST_CASE("epubToIndex: end-to-end EPUB -> compiled index with chapters") {
    const std::string container =
        "<?xml version='1.0'?><container><rootfiles>"
        "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
        "</rootfiles></container>";
    const std::string opf =
        "<package><metadata><dc:title>My Book</dc:title><dc:creator>An Author</dc:creator></metadata>"
        "<manifest>"
        "<item id=\"c1\" href=\"c1.xhtml\" media-type=\"application/xhtml+xml\"/>"
        "<item id=\"c2\" href=\"c2.xhtml\" media-type=\"application/xhtml+xml\"/>"
        "</manifest>"
        "<spine><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>";
    const std::string c1 = "<html><body><p>The cat sat.</p></body></html>";
    const std::string c2 = "<html><body><p>It ran fast.</p></body></html>";

    auto epub = makeEpub({
        {"mimetype", "application/epub+zip"},
        {"META-INF/container.xml", container},
        {"OEBPS/content.opf", opf},
        {"OEBPS/c1.xhtml", c1},
        {"OEBPS/c2.xhtml", c2},
    });

    std::vector<std::uint8_t> bytes = epubToIndex(epub, 1234, 5678);
    REQUIRE(!bytes.empty());

    CompiledIndex idx = CompiledIndex::parse(bytes);
    REQUIRE(idx.ok());
    CHECK(idx.meta().title == "My Book");
    CHECK(idx.meta().author == "An Author");
    CHECK(idx.meta().sourceSize == 1234);
    CHECK(idx.meta().sourceMtime == 5678);

    Document d = idx.toDocument();
    REQUIRE(d.size() == 6); // The cat sat. | It ran fast.
    CHECK(d.tokens[0].text == "The");
    CHECK(d.tokens[3].text == "It");
    CHECK(d.tokens[3].has(FLAG_CHAPTER_START));
    CHECK_FALSE(d.tokens[0].has(FLAG_CHAPTER_START));

    REQUIRE(idx.chapters().size() == 2);
    CHECK(idx.chapters()[0].wordOffset == 0);
    CHECK(idx.chapters()[1].wordOffset == 3);
}

TEST_CASE("epubToIndex: invalid bytes / no container -> empty") {
    CHECK(epubToIndex(std::vector<std::uint8_t>{1, 2, 3}, 0, 0).empty());
    auto noContainer = makeEpub({{"random.txt", "hello"}});
    CHECK(epubToIndex(noContainer, 0, 0).empty());
}

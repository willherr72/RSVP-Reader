#include "doctest.h"
#include "rsvp/index.hpp"
#include "rsvp/byteio.hpp"
#include "rsvp/tokenize.hpp"
using namespace rsvp;

TEST_CASE("serializeIndex writes magic, version, and the header counts") {
    Document d = tokenizePlainText("The cat sat. It ran."); // 5 words
    DocMeta meta;
    meta.title = "T"; meta.author = "A";
    meta.sourceSize = 20; meta.sourceMtime = 12345;
    std::vector<std::uint8_t> b = serializeIndex(d, meta, /*chapters*/{}, /*seekInterval*/4);

    CHECK(b[0] == 'R'); CHECK(b[1] == 'S'); CHECK(b[2] == 'V'); CHECK(b[3] == 'I');
    std::size_t off = 4;
    CHECK(byteio::getU16(b, off) == 1);       // version
    CHECK(byteio::getU16(b, off) == 0);       // flags
    CHECK(byteio::getU32(b, off) == 20);      // sourceSize
    CHECK(byteio::getU32(b, off) == 12345);   // sourceMtime
    CHECK(byteio::getU32(b, off) == 5);       // wordCount
    CHECK(byteio::getU32(b, off) == 0);       // chapterCount
    CHECK(byteio::getU32(b, off) == 4);       // seekInterval
    CHECK(byteio::getU16(b, off) == 1);       // titleLen
    CHECK(b[off] == 'T'); off += 1;
    CHECK(byteio::getU16(b, off) == 1);       // authorLen
    CHECK(b[off] == 'A'); off += 1;

    // No chapters, so the seek table follows the author field immediately.
    CHECK(byteio::getU32(b, off) == 2);   // seekCount: 5 words / interval 4 -> entries at words 0 and 4
    CHECK(byteio::getU32(b, off) == 0);   // entry 0: byte offset 0 in the token stream
    CHECK(byteio::getU32(b, off) == 24);  // entry 1: The(6)+cat(6)+sat.(7)+It(5) = 24
    CHECK(b.size() == 77);                // 34 header + 12 seek table + 31 token stream
}

TEST_CASE("serialize -> parse round-trips document, flags, and metadata") {
    Document d = tokenizePlainText("The cat sat. It ran.\n\nEnd here."); // 7 words
    DocMeta meta;
    meta.title = "Title"; meta.author = "Auth";
    meta.sourceSize = 99; meta.sourceMtime = 7;
    std::vector<Chapter> chapters = { {0, "Start"}, {3, "Next"} };

    std::vector<std::uint8_t> b = serializeIndex(d, meta, chapters, 4);
    CompiledIndex idx = CompiledIndex::parse(b);
    REQUIRE(idx.ok());

    CHECK(idx.meta().title == "Title");
    CHECK(idx.meta().author == "Auth");
    CHECK(idx.meta().sourceSize == 99);
    CHECK(idx.meta().sourceMtime == 7);
    CHECK(idx.wordCount() == d.size());
    REQUIRE(idx.chapters().size() == 2);
    CHECK(idx.chapters()[0].wordOffset == 0);
    CHECK(idx.chapters()[0].title == "Start");
    CHECK(idx.chapters()[1].wordOffset == 3);
    CHECK(idx.chapters()[1].title == "Next");

    Document back = idx.toDocument();
    REQUIRE(back.size() == d.size());
    for (std::size_t i = 0; i < d.size(); ++i) {
        CHECK(back.tokens[i].text == d.tokens[i].text);
        CHECK(back.tokens[i].flags == d.tokens[i].flags);
    }
}

TEST_CASE("parse rejects bad magic and truncated buffers") {
    std::vector<std::uint8_t> badMagic = {'X','X','X','X', 1,0};
    CHECK_FALSE(CompiledIndex::parse(badMagic).ok());
    std::vector<std::uint8_t> tooShort = {'R','S','V','I'};
    CHECK_FALSE(CompiledIndex::parse(tooShort).ok());
}

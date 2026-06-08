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
}

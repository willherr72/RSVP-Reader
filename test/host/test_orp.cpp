#include "doctest.h"
#include "rsvp/orp.hpp"
using namespace rsvp;

TEST_CASE("orpIndex pivots by length bucket") {
    CHECK(orpIndex("")              == 0);
    CHECK(orpIndex("a")             == 0);
    CHECK(orpIndex("cat")           == 0);
    CHECK(orpIndex("read")          == 1);
    CHECK(orpIndex("words")         == 1);
    CHECK(orpIndex("reading")       == 2);
    CHECK(orpIndex("wonderful")     == 2);
    CHECK(orpIndex("incredible")    == 3);
    CHECK(orpIndex("extraordinary") == 3);
}

TEST_CASE("orpSplit cuts around the pivot letter") {
    auto a = orpSplit("reading");
    CHECK(a.pre == "re"); CHECK(a.orp == "a"); CHECK(a.post == "ding");

    auto b = orpSplit("cat");
    CHECK(b.pre == "");   CHECK(b.orp == "c"); CHECK(b.post == "at");

    auto c = orpSplit("read");
    CHECK(c.pre == "r");  CHECK(c.orp == "e"); CHECK(c.post == "ad");

    auto e = orpSplit("");
    CHECK(e.pre == "");   CHECK(e.orp == "");  CHECK(e.post == "");
}

TEST_CASE("orpSplit respects UTF-8 code points") {
    auto s = orpSplit("caf\xC3\xA9"); // "café" (é = U+00E9), 4 code points
    CHECK(s.pre  == "c");
    CHECK(s.orp  == "a");
    CHECK(s.post == "f\xC3\xA9");
}

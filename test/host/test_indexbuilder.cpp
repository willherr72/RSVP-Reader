#include "doctest.h"
#include "rsvp/indexbuilder.hpp"
#include "rsvp/index.hpp"
using namespace rsvp;

TEST_CASE("IndexBuilder round-trips tokens, chapters, meta via CompiledIndex") {
    DocMeta m; m.title = "T"; m.author = "A"; m.sourceSize = 10; m.sourceMtime = 20;
    IndexBuilder ib(m, 4);
    ib.startChapter("Ch1");
    ib.addToken("hello", FLAG_NONE);
    ib.addToken("world.", FLAG_SENTENCE_END);
    ib.startChapter("Ch2");
    ib.addToken("next", FLAG_CHAPTER_START);

    auto ci = CompiledIndex::parse(ib.finish());
    REQUIRE(ci.ok());
    CHECK(ci.wordCount() == 3);
    CHECK(ci.meta().title == "T");
    CHECK(ci.meta().sourceSize == 10);
    CHECK(ci.at(0).text == "hello");
    CHECK(ci.at(1).text == "world.");
    CHECK(ci.at(1).has(FLAG_SENTENCE_END));
    CHECK(ci.at(2).text == "next");
    REQUIRE(ci.chapters().size() == 2);
    CHECK(ci.chapters()[0].wordOffset == 0);
    CHECK(ci.chapters()[0].title == "Ch1");
    CHECK(ci.chapters()[1].wordOffset == 2);
}

TEST_CASE("serializeIndex output equals the equivalent IndexBuilder output") {
    DocMeta m; m.title = "Bk"; m.author = "Au"; m.sourceSize = 1; m.sourceMtime = 2;
    Document doc;
    doc.tokens = { Token{"a", FLAG_NONE}, Token{"b.", FLAG_SENTENCE_END}, Token{"c", FLAG_NONE} };
    std::vector<Chapter> chs = { Chapter{0, "One"}, Chapter{2, "Two"} };

    IndexBuilder ib(m);
    ib.startChapter("One");
    ib.addToken("a", FLAG_NONE);
    ib.addToken("b.", FLAG_SENTENCE_END);
    ib.startChapter("Two");
    ib.addToken("c", FLAG_NONE);

    CHECK(serializeIndex(doc, m, chs) == ib.finish());   // byte-identical
}

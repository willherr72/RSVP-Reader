#include "doctest.h"
#include "rsvp/htmltext.hpp"
#include "rsvp/tokenize.hpp"
using namespace rsvp;

TEST_CASE("htmlToText: strips inline tags, keeps text") {
    CHECK(htmlToText("<p>Hello <b>brave</b> world.</p>") == "Hello brave world.");
}

TEST_CASE("htmlToText: block elements become paragraph breaks") {
    CHECK(htmlToText("<p>One.</p><p>Two.</p>") == "One.\n\nTwo.");
    CHECK(htmlToText("<h1>Title</h1><p>Body here.</p>") == "Title\n\nBody here.");
}

TEST_CASE("htmlToText: decodes entities and collapses source whitespace") {
    CHECK(htmlToText("<p>Tom &amp; Jerry   are\n\n  friends.</p>") == "Tom & Jerry are friends.");
}

TEST_CASE("htmlToText: drops script and style content") {
    CHECK(htmlToText("<style>p{color:red}</style><p>Hi.</p>") == "Hi.");
    CHECK(htmlToText("<script>var a='<p>x</p>';</script><p>Real.</p>") == "Real.");
}

TEST_CASE("htmlToText: <br> is a soft space, not a paragraph") {
    CHECK(htmlToText("<p>line one<br>line two</p>") == "line one line two");
}

TEST_CASE("html pipeline: htmlToText feeds tokenizePlainText into a Document") {
    Document d = tokenizePlainText(htmlToText("<h1>Ch</h1><p>The cat sat.</p><p>It ran.</p>"));
    // text == "Ch\n\nThe cat sat.\n\nIt ran." -> tokens: Ch, The, cat, sat., It, ran.
    REQUIRE(d.size() == 6);
    CHECK(d.tokens[0].text == "Ch");
    CHECK(d.tokens[0].has(FLAG_PARAGRAPH_END));   // "Ch" precedes a blank line
    CHECK(d.tokens[3].text == "sat.");
    CHECK(d.tokens[3].has(FLAG_SENTENCE_END));
    CHECK(d.tokens[3].has(FLAG_PARAGRAPH_END));
    CHECK(d.tokens[5].text == "ran.");
    CHECK(d.tokens[5].has(FLAG_SENTENCE_END));
    CHECK_FALSE(d.tokens[5].has(FLAG_PARAGRAPH_END)); // last word
}

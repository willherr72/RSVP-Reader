#include "doctest.h"
#include "rsvp/tokenize.hpp"
using namespace rsvp;

TEST_CASE("tokenize: empty / whitespace-only text yields no tokens") {
    CHECK(tokenizePlainText("").empty());
    CHECK(tokenizePlainText("   \n\t  ").empty());
}

TEST_CASE("tokenize: splits on whitespace, keeps punctuation attached") {
    Document d = tokenizePlainText("The cat sat.");
    REQUIRE(d.size() == 3);
    CHECK(d.tokens[0].text == "The");
    CHECK(d.tokens[1].text == "cat");
    CHECK(d.tokens[2].text == "sat.");
}

TEST_CASE("tokenize: marks sentence ends (. ! ?)") {
    Document d = tokenizePlainText("Hi there. Go now! Really?");
    REQUIRE(d.size() == 5);
    CHECK_FALSE(d.tokens[0].has(FLAG_SENTENCE_END)); // Hi
    CHECK(d.tokens[1].has(FLAG_SENTENCE_END));       // there.
    CHECK_FALSE(d.tokens[2].has(FLAG_SENTENCE_END)); // Go
    CHECK(d.tokens[3].has(FLAG_SENTENCE_END));       // now!
    CHECK(d.tokens[4].has(FLAG_SENTENCE_END));       // Really?
}

TEST_CASE("tokenize: sentence end allows trailing quotes/brackets") {
    Document d = tokenizePlainText("He said \"go.\" (Yes.)");
    REQUIRE(d.size() == 4);
    CHECK(d.tokens[2].has(FLAG_SENTENCE_END));       // "go."
    CHECK(d.tokens[3].has(FLAG_SENTENCE_END));       // (Yes.)
}

TEST_CASE("tokenize: blank line marks paragraph end on the preceding word") {
    Document d = tokenizePlainText("First para end.\n\nSecond para.");
    REQUIRE(d.size() == 5);
    CHECK(d.tokens[2].has(FLAG_PARAGRAPH_END));       // end. (blank line follows)
    CHECK(d.tokens[2].has(FLAG_SENTENCE_END));        // also a sentence end
    CHECK_FALSE(d.tokens[4].has(FLAG_PARAGRAPH_END)); // last word, no trailing blank line
}

TEST_CASE("tokenize: a single newline is not a paragraph break") {
    Document d = tokenizePlainText("line one\nline two");
    REQUIRE(d.size() == 4);
    CHECK_FALSE(d.tokens[1].has(FLAG_PARAGRAPH_END)); // "one"
}

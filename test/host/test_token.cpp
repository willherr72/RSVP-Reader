#include "doctest.h"
#include "rsvp/token.hpp"
using namespace rsvp;

TEST_CASE("Token flags and Document basics") {
    Token t{"Hello.", FLAG_SENTENCE_END};
    CHECK(t.has(FLAG_SENTENCE_END));
    CHECK_FALSE(t.has(FLAG_PARAGRAPH_END));

    Document d;
    CHECK(d.empty());
    d.tokens.push_back(t);
    CHECK(d.size() == 1);
    CHECK_FALSE(d.empty());
}

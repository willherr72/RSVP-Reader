#include "doctest.h"
#include "rsvp/entity.hpp"
using namespace rsvp;

TEST_CASE("decodeEntities: basic named entities") {
    CHECK(decodeEntities("Tom &amp; Jerry") == "Tom & Jerry");
    CHECK(decodeEntities("&lt;tag&gt;") == "<tag>");
    CHECK(decodeEntities("say &quot;hi&quot;") == "say \"hi\"");
    CHECK(decodeEntities("it&apos;s") == "it's");
}

TEST_CASE("decodeEntities: numeric decimal and hex") {
    CHECK(decodeEntities("&#65;&#x42;") == "AB");
    CHECK(decodeEntities("caf&#233;") == "caf\xC3\xA9"); // é
}

TEST_CASE("decodeEntities: nbsp becomes a space; punctuation entities") {
    CHECK(decodeEntities("a&nbsp;b") == "a b");
    CHECK(decodeEntities("yes&mdash;no") == "yes\xE2\x80\x94no"); // em dash U+2014
    CHECK(decodeEntities("wait&hellip;") == "wait\xE2\x80\xA6"); // ellipsis U+2026
}

TEST_CASE("decodeEntities: unknown / malformed entities are left literal") {
    CHECK(decodeEntities("A&unknown;B") == "A&unknown;B");
    CHECK(decodeEntities("100 &amp 200") == "100 &amp 200"); // no semicolon
    CHECK(decodeEntities("ends with &") == "ends with &");
    CHECK(decodeEntities("&#;") == "&#;");                   // empty numeric
}

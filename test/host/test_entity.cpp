#include "doctest.h"
#include "rsvp/entity.hpp"
using namespace rsvp;

TEST_CASE("decodeEntities: basic named entities") {
    CHECK(decodeEntities("Tom &amp; Jerry") == "Tom & Jerry");
    CHECK(decodeEntities("&lt;tag&gt;") == "<tag>");
    CHECK(decodeEntities("say &quot;hi&quot;") == "say \"hi\"");
    CHECK(decodeEntities("it&apos;s") == "it's");
}

TEST_CASE("normalizeUnicodePunctuation maps smart punctuation to ASCII") {
    CHECK(normalizeUnicodePunctuation("\xE2\x80\x98") == "'");    // ' U+2018
    CHECK(normalizeUnicodePunctuation("\xE2\x80\x99") == "'");    // ' U+2019
    CHECK(normalizeUnicodePunctuation("\xE2\x80\x9C") == "\"");   // " U+201C
    CHECK(normalizeUnicodePunctuation("\xE2\x80\x9D") == "\"");   // " U+201D
    CHECK(normalizeUnicodePunctuation("\xE2\x80\x93") == "-");    // en dash U+2013
    CHECK(normalizeUnicodePunctuation("\xE2\x80\x94") == "--");   // em dash U+2014
    CHECK(normalizeUnicodePunctuation("\xE2\x80\xA6") == "...");  // ellipsis U+2026
    CHECK(normalizeUnicodePunctuation(std::string("a") + "\xC2\xA0" + "b") == "a b");  // nbsp
}

TEST_CASE("normalizeUnicodePunctuation leaves ASCII alone") {
    CHECK(normalizeUnicodePunctuation("It's a \"test\" - ok.") == "It's a \"test\" - ok.");
    CHECK(normalizeUnicodePunctuation("") == "");
}

TEST_CASE("normalizeUnicodePunctuation handles a full smart-quoted sentence") {
    std::string in = std::string("\xE2\x80\x9C") + "I can" + "\xE2\x80\x99" + "t," +
                     "\xE2\x80\x9D" + " she said" + "\xE2\x80\xA6";
    CHECK(normalizeUnicodePunctuation(in) == "\"I can't,\" she said...");
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

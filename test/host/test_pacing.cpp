#include "doctest.h"
#include "rsvp/pacing.hpp"
using namespace rsvp;

TEST_CASE("baseWordMs from WPM") {
    CHECK(baseWordMs(300) == 200);
    CHECK(baseWordMs(600) == 100);
    CHECK(baseWordMs(100) == 600);
    CHECK(baseWordMs(0)   == 60000); // guarded: wpm clamped to 1
}

TEST_CASE("wordDurationMs applies flag factors") {
    PacingConfig cfg; // wpm 300 -> 200 ms base
    CHECK(wordDurationMs(Token{"word", FLAG_NONE},          cfg) == 200);
    CHECK(wordDurationMs(Token{"end.", FLAG_SENTENCE_END},  cfg) == 400);
    CHECK(wordDurationMs(Token{"para", FLAG_PARAGRAPH_END}, cfg) == 500);
    CHECK(wordDurationMs(Token{"Chap", FLAG_CHAPTER_START}, cfg) == 600);
}

TEST_CASE("wordDurationMs uses the strongest factor when multiple flags set") {
    PacingConfig cfg;
    Token t{"done.", static_cast<std::uint8_t>(FLAG_SENTENCE_END | FLAG_PARAGRAPH_END)};
    CHECK(wordDurationMs(t, cfg) == 500); // max(2.0, 2.5) * 200
}

TEST_CASE("wordDurationMs adds a long-word bonus when enabled") {
    PacingConfig cfg;
    cfg.longWordPerCharMs = 10.0;
    cfg.longWordThreshold = 8;
    CHECK(wordDurationMs(Token{"extraordinary", FLAG_NONE}, cfg) == 250); // 200 + (13-8)*10
}

TEST_CASE("wordDurationMs clamps to the minimum") {
    PacingConfig cfg;
    cfg.wpm = 2000; // base 30 ms
    CHECK(wordDurationMs(Token{"x", FLAG_NONE}, cfg) == 60);
}

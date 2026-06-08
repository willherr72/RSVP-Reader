#include "doctest.h"
#include "rsvp/player.hpp"
#include <initializer_list>
using namespace rsvp;

static Document plainDoc(std::initializer_list<const char*> words) {
    Document d;
    for (auto w : words) d.tokens.push_back(Token{w, FLAG_NONE});
    return d;
}

TEST_CASE("Player initial state") {
    Document d = plainDoc({"one", "two", "three"});
    Player p(d, PacingConfig{}); // 200 ms/word
    CHECK(p.index() == 0);
    CHECK(p.size() == 3);
    CHECK_FALSE(p.isPlaying());
    CHECK_FALSE(p.isFinished());
    CHECK(p.current().text == "one");
}

TEST_CASE("Player advances on tick while playing") {
    Document d = plainDoc({"one", "two", "three"});
    Player p(d, PacingConfig{});
    p.play();
    CHECK(p.isPlaying());
    CHECK(p.tick(199) == 0);
    CHECK(p.index() == 0);
    CHECK(p.tick(1) == 1);          // total 200 -> advance one
    CHECK(p.index() == 1);
    CHECK(p.current().text == "two");
}

TEST_CASE("Player finishes after the last word completes") {
    Document d = plainDoc({"one", "two", "three"});
    Player p(d, PacingConfig{});
    p.play();
    CHECK(p.tick(200) == 1); CHECK(p.index() == 1);
    CHECK(p.tick(200) == 1); CHECK(p.index() == 2);
    CHECK(p.tick(200) == 0);        // completing the last word finishes
    CHECK(p.index() == 2);
    CHECK(p.isFinished());
    CHECK_FALSE(p.isPlaying());
}

TEST_CASE("Player pause halts advance and resumes continuously") {
    Document d = plainDoc({"one", "two", "three"});
    Player p(d, PacingConfig{});
    p.play();
    CHECK(p.tick(100) == 0);
    p.pause();
    CHECK_FALSE(p.isPlaying());
    CHECK(p.tick(1000) == 0);       // paused: no advance
    CHECK(p.index() == 0);
    p.play();
    CHECK(p.tick(100) == 1);        // 100 + 100 = 200 -> advance
    CHECK(p.index() == 1);
}

TEST_CASE("Player seek clamps and resets the timer") {
    Document d = plainDoc({"one", "two", "three"});
    Player p(d, PacingConfig{});
    p.seek(99);
    CHECK(p.index() == 2);
    p.seek(1);
    CHECK(p.index() == 1);
    p.play();
    CHECK(p.tick(199) == 0);        // timer was reset by seek
    CHECK(p.tick(1) == 1);
    CHECK(p.index() == 2);
}

TEST_CASE("Player progress") {
    Document d = plainDoc({"one", "two", "three"});
    Player p(d, PacingConfig{});
    CHECK(p.progress() == doctest::Approx(0.0));
    p.seek(1);
    CHECK(p.progress() == doctest::Approx(1.0 / 3.0));
    p.seek(2);
    p.play();
    p.tick(200);                    // completes last word -> finished
    CHECK(p.isFinished());
    CHECK(p.progress() == doctest::Approx(1.0));
}

TEST_CASE("Player with an empty document is finished and inert") {
    Document d;
    Player p(d, PacingConfig{});
    CHECK(p.isFinished());
    p.play();
    CHECK_FALSE(p.isPlaying());
    CHECK(p.tick(1000) == 0);
}

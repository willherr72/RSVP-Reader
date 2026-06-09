#include "doctest.h"
#include "rsvp/player.hpp"
#include "rsvp/index.hpp"
#include "rsvp/indexbuilder.hpp"
#include <initializer_list>
using namespace rsvp;

// The Player holds a const CompiledIndex&, so each test keeps the index in a named
// local that outlives the Player (a temporary would dangle).
static CompiledIndex plainIndex(std::initializer_list<const char*> words) {
    IndexBuilder ib(DocMeta{});
    for (auto w : words) ib.addToken(w, FLAG_NONE);
    return CompiledIndex::parse(ib.finish());
}

static CompiledIndex makeIndex(std::initializer_list<Token> toks) {
    IndexBuilder ib(DocMeta{});
    for (const auto& t : toks) ib.addToken(t.text, t.flags);
    return CompiledIndex::parse(ib.finish());
}

TEST_CASE("Player initial state") {
    CompiledIndex idx = plainIndex({"one", "two", "three"});
    Player p(idx, PacingConfig{}); // 200 ms/word
    CHECK(p.index() == 0);
    CHECK(p.size() == 3);
    CHECK_FALSE(p.isPlaying());
    CHECK_FALSE(p.isFinished());
    CHECK(p.current().text == "one");
}

TEST_CASE("Player advances on tick while playing") {
    CompiledIndex idx = plainIndex({"one", "two", "three"});
    Player p(idx, PacingConfig{});
    p.play();
    CHECK(p.isPlaying());
    CHECK(p.tick(199) == 0);
    CHECK(p.index() == 0);
    CHECK(p.tick(1) == 1);          // total 200 -> advance one
    CHECK(p.index() == 1);
    CHECK(p.current().text == "two");
}

TEST_CASE("Player finishes after the last word completes") {
    CompiledIndex idx = plainIndex({"one", "two", "three"});
    Player p(idx, PacingConfig{});
    p.play();
    CHECK(p.tick(200) == 1); CHECK(p.index() == 1);
    CHECK(p.tick(200) == 1); CHECK(p.index() == 2);
    CHECK(p.tick(200) == 0);        // completing the last word finishes
    CHECK(p.index() == 2);
    CHECK(p.isFinished());
    CHECK_FALSE(p.isPlaying());
}

TEST_CASE("Player pause halts advance and resumes continuously") {
    CompiledIndex idx = plainIndex({"one", "two", "three"});
    Player p(idx, PacingConfig{});
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
    CompiledIndex idx = plainIndex({"one", "two", "three"});
    Player p(idx, PacingConfig{});
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
    CompiledIndex idx = plainIndex({"one", "two", "three"});
    Player p(idx, PacingConfig{});
    CHECK(p.progress() == doctest::Approx(0.0));
    p.seek(1);
    CHECK(p.progress() == doctest::Approx(1.0 / 3.0));
    p.seek(2);
    p.play();
    p.tick(200);                    // completes last word -> finished
    CHECK(p.isFinished());
    CHECK(p.progress() == doctest::Approx(1.0));
}

TEST_CASE("Player togglePlay flips play state and respects finish") {
    CompiledIndex idx = plainIndex({"one", "two"});
    Player p(idx, PacingConfig{});
    CHECK_FALSE(p.isPlaying());
    p.togglePlay(); CHECK(p.isPlaying());        // paused -> playing
    p.togglePlay(); CHECK_FALSE(p.isPlaying());  // playing -> paused
    p.play();
    p.tick(200); p.tick(200);                    // finish the 2-word doc
    CHECK(p.isFinished());
    p.togglePlay();
    CHECK_FALSE(p.isPlaying());                  // finished player is not resurrected
}

TEST_CASE("Player tick after finish is inert") {
    CompiledIndex idx = plainIndex({"one", "two"});
    Player p(idx, PacingConfig{});
    p.play();
    CHECK(p.tick(200) == 1);
    CHECK(p.tick(200) == 0);                     // completes last word -> finished
    CHECK(p.isFinished());
    CHECK(p.tick(1000) == 0);                    // further ticks do nothing
    CHECK(p.index() == 1);
    CHECK(p.isFinished());
}

TEST_CASE("Player with an empty document is finished and inert") {
    CompiledIndex idx = plainIndex({});
    Player p(idx, PacingConfig{});
    CHECK(p.isFinished());
    p.play();
    CHECK_FALSE(p.isPlaying());
    CHECK(p.tick(1000) == 0);
}

// Sentences: [The cat sat.] [It ran.] [End]
static CompiledIndex sentenceIndex() {
    return makeIndex({
        {"The",  FLAG_NONE}, {"cat", FLAG_NONE}, {"sat.", FLAG_SENTENCE_END},
        {"It",   FLAG_NONE}, {"ran.", FLAG_SENTENCE_END},
        {"End",  FLAG_NONE},
    });
}

TEST_CASE("nextSentence jumps to the start of the following sentence") {
    CompiledIndex idx = sentenceIndex();
    Player p(idx, PacingConfig{});
    p.nextSentence(); CHECK(p.index() == 3);  // after "sat."
    p.nextSentence(); CHECK(p.index() == 5);  // after "ran."
    p.nextSentence(); CHECK(p.index() == 5);  // none left -> clamps to last
}

TEST_CASE("prevSentence goes to current start, then previous start") {
    CompiledIndex idx = sentenceIndex();
    Player p(idx, PacingConfig{});
    p.seek(4); p.prevSentence(); CHECK(p.index() == 3); // start of current sentence
    p.prevSentence();            CHECK(p.index() == 0); // previous sentence start
    p.prevSentence();            CHECK(p.index() == 0); // clamp at first
    p.seek(5); p.prevSentence(); CHECK(p.index() == 3); // from "End" -> sentence 2 start
}

TEST_CASE("Player setConfig adjusts pacing") {
    CompiledIndex idx = plainIndex({"one", "two"});
    Player p(idx, PacingConfig{});       // 300 wpm -> 200 ms/word
    CHECK(p.config().wpm == 300);
    PacingConfig slow = p.config();
    slow.wpm = 150;                      // 400 ms/word
    p.setConfig(slow);
    CHECK(p.config().wpm == 150);
    p.play();
    CHECK(p.tick(399) == 0);             // 399 < 400 ms, still on word 0
    CHECK(p.index() == 0);
    CHECK(p.tick(1) == 1);               // reaches 400 ms -> advance
    CHECK(p.index() == 1);
}

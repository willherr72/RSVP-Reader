#pragma once
#include "rsvp/token.hpp"
#include "rsvp/pacing.hpp"
#include <cstddef>

namespace rsvp {

// Drives playback over a Document. Time is supplied externally via tick(dtMs),
// so it is fully deterministic and testable with no real clock.
class Player {
public:
    Player(const Document& doc, PacingConfig cfg);

    void play();
    void pause();
    void togglePlay();

    bool isPlaying()  const { return playing_; }
    bool isFinished() const { return finished_; }

    std::size_t  index() const { return index_; }
    std::size_t  size()  const { return doc_.size(); }
    const Token& current() const { return doc_.tokens[index_]; } // precondition: !empty
    double       progress() const;

    // Advance the clock by dtMs. While playing, advances through any tokens whose
    // duration has elapsed. Returns the number of tokens advanced past.
    int  tick(int dtMs);

    void seek(std::size_t i);   // clamps to [0, size-1]; resets the word timer
    void nextSentence();        // jump to the start of the following sentence
    void prevSentence();        // jump to the start of the current/previous sentence

private:
    const Document& doc_;
    PacingConfig    cfg_;
    std::size_t     index_    = 0;
    int             elapsed_  = 0;
    bool            playing_  = false;
    bool            finished_ = false;
};

} // namespace rsvp

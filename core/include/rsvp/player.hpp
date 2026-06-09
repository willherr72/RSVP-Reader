#pragma once
#include "rsvp/index.hpp"
#include "rsvp/pacing.hpp"
#include <cstddef>

namespace rsvp {

// Drives playback over a CompiledIndex, reading the current word on demand (no full
// Document). Time is supplied externally via tick(dtMs), so it is fully deterministic
// and testable with no real clock.
class Player {
public:
    Player(const CompiledIndex& idx, PacingConfig cfg);

    void play();
    void pause();
    void togglePlay();

    bool isPlaying()  const { return playing_; }
    bool isFinished() const { return finished_; }

    std::size_t  index() const { return index_; }
    std::size_t  size()  const { return idx_.wordCount(); }
    // The token at the current index, materialized from the index and cached on each
    // index change. Remains valid after finish (stays on the last token).
    const Token& current() const { return current_; }
    double       progress() const;

    // Pacing config access — setConfig adjusts pacing live (e.g. WPM) and takes
    // effect from the current word's remaining time onward.
    const PacingConfig& config() const { return cfg_; }
    void setConfig(const PacingConfig& cfg) { cfg_ = cfg; }

    // Advance the clock by dtMs. While playing, advances through any tokens whose
    // duration has elapsed. Returns the number of index advances (tokens moved TO);
    // completing the final token is NOT counted — use isFinished() to detect the end.
    int  tick(int dtMs);

    void seek(std::size_t i);   // clamps to [0, size-1]; resets the word timer
    void nextSentence();        // jump to the start of the following sentence
    void prevSentence();        // jump to the start of the current/previous sentence

private:
    const CompiledIndex& idx_;
    PacingConfig         cfg_;
    Token                current_{};
    std::size_t          index_    = 0;
    int                  elapsed_  = 0;
    bool                 playing_  = false;
    bool                 finished_ = false;
};

} // namespace rsvp

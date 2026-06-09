#include "rsvp/player.hpp"

namespace rsvp {

Player::Player(const CompiledIndex& idx, PacingConfig cfg)
    : idx_(idx), cfg_(cfg), finished_(idx.wordCount() == 0) {
    if (idx_.wordCount() > 0) current_ = idx_.at(0);
}

void Player::play()       { if (idx_.wordCount() > 0 && !finished_) playing_ = true; }
void Player::pause()      { playing_ = false; }
void Player::togglePlay() { if (playing_) pause(); else play(); }

// Fraction of tokens preceding the current one (index/size): 0.0 at the start and
// when empty, 1.0 only once finished. (Not "fraction of tokens seen".)
double Player::progress() const {
    if (idx_.wordCount() == 0) return 0.0;
    if (finished_)             return 1.0;
    return static_cast<double>(index_) / static_cast<double>(idx_.wordCount());
}

int Player::tick(int dtMs) {
    if (!playing_ || finished_ || idx_.wordCount() == 0 || dtMs <= 0) return 0;
    elapsed_ += dtMs;
    int advanced = 0;
    while (true) {
        int dur = wordDurationMs(current_, cfg_);
        if (dur < 1) dur = 1;   // defensive: guarantee forward progress even if a
                                // pathological PacingConfig yields a zero duration
        if (elapsed_ < dur) break;
        elapsed_ -= dur;
        if (index_ + 1 >= idx_.wordCount()) {   // last word completed
            finished_ = true; playing_ = false; elapsed_ = 0;
            break;
        }
        ++index_; current_ = idx_.at(index_); ++advanced;
    }
    return advanced;
}

void Player::seek(std::size_t i) {
    if (idx_.wordCount() == 0) return;
    if (i >= idx_.wordCount()) i = idx_.wordCount() - 1;
    index_ = i; current_ = idx_.at(index_); elapsed_ = 0; finished_ = false;
}

void Player::nextSentence() {
    if (idx_.wordCount() == 0) return;
    std::size_t i = index_;
    for (; i < idx_.wordCount(); ++i)
        if (idx_.at(i).has(FLAG_SENTENCE_END)) break;
    if (i < idx_.wordCount())
        seek(i + 1 < idx_.wordCount() ? i + 1 : idx_.wordCount() - 1);
    else
        seek(idx_.wordCount() - 1);
}

void Player::prevSentence() {
    if (idx_.wordCount() == 0) return;
    const std::size_t cur = index_;
    std::size_t s = 0;                          // start of the current sentence
    for (std::size_t i = cur; i-- > 0; )
        if (idx_.at(i).has(FLAG_SENTENCE_END)) { s = i + 1; break; }
    if (s < cur) { seek(s); return; }           // mid-sentence -> jump to its start
    if (s == 0)  { seek(0); return; }           // no sentence-end before current -> first sentence start
    std::size_t ps = 0;                         // start of the previous sentence
    for (std::size_t i = s - 1; i-- > 0; )
        if (idx_.at(i).has(FLAG_SENTENCE_END)) { ps = i + 1; break; }
    seek(ps);
}

} // namespace rsvp

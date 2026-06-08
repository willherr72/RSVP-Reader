#include "rsvp/player.hpp"

namespace rsvp {

Player::Player(const Document& doc, PacingConfig cfg)
    : doc_(doc), cfg_(cfg), finished_(doc.empty()) {}

void Player::play()       { if (!doc_.empty() && !finished_) playing_ = true; }
void Player::pause()      { playing_ = false; }
void Player::togglePlay() { if (playing_) pause(); else play(); }

double Player::progress() const {
    if (doc_.empty()) return 0.0;
    if (finished_)    return 1.0;
    return static_cast<double>(index_) / static_cast<double>(doc_.size());
}

int Player::tick(int dtMs) {
    if (!playing_ || finished_ || doc_.empty() || dtMs <= 0) return 0;
    elapsed_ += dtMs;
    int advanced = 0;
    while (true) {
        const int dur = wordDurationMs(doc_.tokens[index_], cfg_);
        if (elapsed_ < dur) break;
        elapsed_ -= dur;
        if (index_ + 1 >= doc_.size()) {       // last word completed
            finished_ = true; playing_ = false; elapsed_ = 0;
            break;
        }
        ++index_; ++advanced;
    }
    return advanced;
}

void Player::seek(std::size_t i) {
    if (doc_.empty()) return;
    if (i >= doc_.size()) i = doc_.size() - 1;
    index_ = i; elapsed_ = 0; finished_ = false;
}

void Player::nextSentence() {
    if (doc_.empty()) return;
    std::size_t i = index_;
    for (; i < doc_.size(); ++i)
        if (doc_.tokens[i].has(FLAG_SENTENCE_END)) break;
    if (i < doc_.size())
        seek(i + 1 < doc_.size() ? i + 1 : doc_.size() - 1);
    else
        seek(doc_.size() - 1);
}

void Player::prevSentence() {
    if (doc_.empty()) return;
    const std::size_t cur = index_;
    std::size_t s = 0;                          // start of the current sentence
    for (std::size_t i = cur; i-- > 0; )
        if (doc_.tokens[i].has(FLAG_SENTENCE_END)) { s = i + 1; break; }
    if (s < cur) { seek(s); return; }           // mid-sentence -> jump to its start
    if (s == 0)  { seek(0); return; }           // already at first sentence start
    std::size_t ps = 0;                         // start of the previous sentence
    for (std::size_t i = s - 1; i-- > 0; )
        if (doc_.tokens[i].has(FLAG_SENTENCE_END)) { ps = i + 1; break; }
    seek(ps);
}

} // namespace rsvp

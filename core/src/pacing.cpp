#include "rsvp/pacing.hpp"
#include "rsvp/utf8.hpp"
#include <algorithm>

namespace rsvp {

int baseWordMs(int wpm) {
    const int w = wpm < 1 ? 1 : wpm;
    return 60000 / w;
}

int wordDurationMs(const Token& tok, const PacingConfig& cfg) {
    double ms = baseWordMs(cfg.wpm);

    const int len = static_cast<int>(utf8::length(tok.text));
    if (cfg.longWordPerCharMs > 0.0 && len > cfg.longWordThreshold)
        ms += (len - cfg.longWordThreshold) * cfg.longWordPerCharMs;

    double factor = 1.0;
    if (tok.has(FLAG_SENTENCE_END))  factor = std::max(factor, cfg.sentenceEndFactor);
    if (tok.has(FLAG_PARAGRAPH_END)) factor = std::max(factor, cfg.paragraphEndFactor);
    if (tok.has(FLAG_CHAPTER_START)) factor = std::max(factor, cfg.chapterStartFactor);
    ms *= factor;

    int out = static_cast<int>(ms + 0.5);
    if (out < cfg.minWordMs) out = cfg.minWordMs;
    return out;
}

} // namespace rsvp

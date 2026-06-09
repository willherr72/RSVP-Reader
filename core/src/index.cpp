#include "rsvp/index.hpp"
#include "rsvp/indexbuilder.hpp"
#include "rsvp/byteio.hpp"

namespace rsvp {
using namespace byteio;

namespace {
// All multi-byte fields in the compiled-index format are little-endian (see byteio.hpp).
constexpr char          kMagic[4] = {'R', 'S', 'V', 'I'};
constexpr std::uint16_t kVersion  = 1;
} // namespace

std::vector<std::uint8_t> serializeIndex(const Document& doc, const DocMeta& meta,
                                         const std::vector<Chapter>& chapters,
                                         std::uint32_t seekInterval) {
    IndexBuilder ib(meta, seekInterval);
    std::size_t ci = 0;
    for (std::size_t i = 0; i < doc.tokens.size(); ++i) {
        while (ci < chapters.size() && chapters[ci].wordOffset == i) ib.startChapter(chapters[ci++].title);
        ib.addToken(doc.tokens[i].text, doc.tokens[i].flags);
    }
    while (ci < chapters.size()) ib.startChapter(chapters[ci++].title);
    return ib.finish();
}

// --- stubs replaced in Tasks 3 and 4 ---
CompiledIndex CompiledIndex::parse(const std::vector<std::uint8_t>& b) {
    CompiledIndex idx;
    std::size_t off = 0;
    auto need = [&](std::size_t k) { return off + k <= b.size(); };

    if (!need(4) || b[0] != 'R' || b[1] != 'S' || b[2] != 'V' || b[3] != 'I') return idx;
    off = 4;
    if (!need(2)) return idx;
    if (getU16(b, off) != kVersion) return idx;
    if (!need(2)) return idx; getU16(b, off);                        // flags
    if (!need(4)) return idx; idx.meta_.sourceSize  = getU32(b, off);
    if (!need(4)) return idx; idx.meta_.sourceMtime = getU32(b, off);
    if (!need(4)) return idx; const std::uint32_t wc = getU32(b, off);
    if (!need(4)) return idx; const std::uint32_t cc = getU32(b, off);
    if (!need(4)) return idx; idx.seekInterval_ = getU32(b, off);
    if (idx.seekInterval_ == 0) return idx;

    if (!need(2)) return idx; const std::uint16_t tl = getU16(b, off);
    if (!need(tl)) return idx; idx.meta_.title.assign(b.begin() + off, b.begin() + off + tl); off += tl;
    if (!need(2)) return idx; const std::uint16_t al = getU16(b, off);
    if (!need(al)) return idx; idx.meta_.author.assign(b.begin() + off, b.begin() + off + al); off += al;

    for (std::uint32_t i = 0; i < cc; ++i) {
        if (!need(4)) return idx; const std::uint32_t wo = getU32(b, off);
        if (!need(2)) return idx; const std::uint16_t cl = getU16(b, off);
        if (!need(cl)) return idx; std::string ct(b.begin() + off, b.begin() + off + cl); off += cl;
        idx.chapters_.push_back(Chapter{wo, ct});
    }

    if (!need(4)) return idx; const std::uint32_t sc = getU32(b, off);
    for (std::uint32_t i = 0; i < sc; ++i) {
        if (!need(4)) return idx;
        idx.seekOffsets_.push_back(getU32(b, off));
    }

    idx.tokenStream_.assign(b.begin() + off, b.end());
    idx.wordCount_ = wc;
    idx.ok_ = true;
    return idx;
}

Document CompiledIndex::toDocument() const {
    Document d;
    if (!ok_) return d;
    std::size_t off = 0;
    for (std::size_t i = 0; i < wordCount_; ++i) {
        if (off + 3 > tokenStream_.size()) break;            // flags(1) + len(2)
        const std::uint8_t flags = tokenStream_[off++];
        const std::uint16_t len = getU16(tokenStream_, off);
        if (off + len > tokenStream_.size()) break;
        std::string w(tokenStream_.begin() + off, tokenStream_.begin() + off + len);
        off += len;
        d.tokens.push_back(Token{w, flags});
    }
    return d;
}

Token CompiledIndex::at(std::size_t wordIndex) const {
    if (!ok_ || wordIndex >= wordCount_) return Token{};
    const std::size_t entry = wordIndex / seekInterval_;
    std::size_t off = (entry < seekOffsets_.size()) ? seekOffsets_[entry] : 0;
    const std::size_t toSkip = wordIndex - entry * seekInterval_;
    for (std::size_t s = 0; s < toSkip; ++s) {
        if (off + 3 > tokenStream_.size()) return Token{};
        off += 1;                                   // flags byte
        const std::uint16_t len = getU16(tokenStream_, off);
        off += len;
    }
    if (off + 3 > tokenStream_.size()) return Token{};
    const std::uint8_t flags = tokenStream_[off++];
    const std::uint16_t len = getU16(tokenStream_, off);
    if (off + len > tokenStream_.size()) return Token{};
    return Token{ std::string(tokenStream_.begin() + off, tokenStream_.begin() + off + len), flags };
}

bool indexMatchesSource(const std::vector<std::uint8_t>& b,
                        std::uint32_t sourceSize, std::uint32_t sourceMtime) {
    if (b.size() < 16) return false;                // magic(4)+ver(2)+flags(2)+size(4)+mtime(4)
    if (b[0] != 'R' || b[1] != 'S' || b[2] != 'V' || b[3] != 'I') return false;
    std::size_t off = 4;
    if (getU16(b, off) != kVersion) return false;   // version
    getU16(b, off);                                 // flags
    const std::uint32_t recSize  = getU32(b, off);
    const std::uint32_t recMtime = getU32(b, off);
    return recSize == sourceSize && recMtime == sourceMtime;
}

} // namespace rsvp

#include "rsvp/index.hpp"
#include "rsvp/byteio.hpp"

namespace rsvp {
using namespace byteio;

namespace {
// All multi-byte fields in the compiled-index format are little-endian (see byteio.hpp).
constexpr char          kMagic[4] = {'R', 'S', 'V', 'I'};
constexpr std::uint16_t kVersion  = 1;

// Append a u16-length-prefixed byte blob, clamping the length to 0xFFFF so the length
// field and the bytes written always agree (an oversized blob is truncated rather than
// corrupting the rest of the stream).
void putBlob16(std::vector<std::uint8_t>& b, const std::string& s) {
    std::size_t len = s.size();
    if (len > 0xFFFFu) len = 0xFFFFu;
    putU16(b, static_cast<std::uint16_t>(len));
    b.insert(b.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(len));
}
} // namespace

std::vector<std::uint8_t> serializeIndex(const Document& doc, const DocMeta& meta,
                                         const std::vector<Chapter>& chapters,
                                         std::uint32_t seekInterval) {
    if (seekInterval == 0) seekInterval = kDefaultSeekInterval;

    // Build the token stream first, recording seek offsets at each interval boundary.
    std::vector<std::uint8_t> ts;
    std::vector<std::uint32_t> seekOffsets;
    for (std::size_t i = 0; i < doc.tokens.size(); ++i) {
        if (i % seekInterval == 0) seekOffsets.push_back(static_cast<std::uint32_t>(ts.size()));
        const Token& t = doc.tokens[i];
        ts.push_back(t.flags);
        putBlob16(ts, t.text);
    }

    std::vector<std::uint8_t> b;
    b.insert(b.end(), kMagic, kMagic + 4);
    putU16(b, kVersion);
    putU16(b, 0); // flags (reserved)
    putU32(b, meta.sourceSize);
    putU32(b, meta.sourceMtime);
    putU32(b, static_cast<std::uint32_t>(doc.tokens.size()));
    putU32(b, static_cast<std::uint32_t>(chapters.size()));
    putU32(b, seekInterval);
    putBlob16(b, meta.title);
    putBlob16(b, meta.author);
    for (const Chapter& c : chapters) {
        putU32(b, c.wordOffset);
        putBlob16(b, c.title);
    }
    putU32(b, static_cast<std::uint32_t>(seekOffsets.size()));
    for (std::uint32_t off : seekOffsets) putU32(b, off);
    b.insert(b.end(), ts.begin(), ts.end());
    return b;
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

Token    CompiledIndex::at(std::size_t) const { return Token{}; }
bool indexMatchesSource(const std::vector<std::uint8_t>&, std::uint32_t, std::uint32_t) { return false; }

} // namespace rsvp

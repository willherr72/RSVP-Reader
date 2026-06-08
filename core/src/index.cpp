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
CompiledIndex CompiledIndex::parse(const std::vector<std::uint8_t>&) { return CompiledIndex{}; }
Document CompiledIndex::toDocument() const { return Document{}; }
Token    CompiledIndex::at(std::size_t) const { return Token{}; }
bool indexMatchesSource(const std::vector<std::uint8_t>&, std::uint32_t, std::uint32_t) { return false; }

} // namespace rsvp

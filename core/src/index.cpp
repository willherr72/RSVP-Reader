#include "rsvp/index.hpp"
#include "rsvp/byteio.hpp"

namespace rsvp {
using namespace byteio;

namespace {
const char     kMagic[4] = {'R', 'S', 'V', 'I'};
constexpr std::uint16_t kVersion = 1;
}

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
        putU16(ts, static_cast<std::uint16_t>(t.text.size()));
        ts.insert(ts.end(), t.text.begin(), t.text.end());
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
    putU16(b, static_cast<std::uint16_t>(meta.title.size()));
    b.insert(b.end(), meta.title.begin(), meta.title.end());
    putU16(b, static_cast<std::uint16_t>(meta.author.size()));
    b.insert(b.end(), meta.author.begin(), meta.author.end());
    for (const Chapter& c : chapters) {
        putU32(b, c.wordOffset);
        putU16(b, static_cast<std::uint16_t>(c.title.size()));
        b.insert(b.end(), c.title.begin(), c.title.end());
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

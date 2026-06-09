#include "rsvp/indexbuilder.hpp"
#include "rsvp/byteio.hpp"

namespace rsvp {
using namespace byteio;

namespace {
// Append a u16-length-prefixed byte blob, clamping the length to 0xFFFF so the
// length field and the bytes written always agree.
void putBlob16(std::vector<std::uint8_t>& b, const std::string& s) {
    std::size_t len = s.size();
    if (len > 0xFFFFu) len = 0xFFFFu;
    putU16(b, static_cast<std::uint16_t>(len));
    b.insert(b.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(len));
}
constexpr char          kMagic[4] = {'R', 'S', 'V', 'I'};
constexpr std::uint16_t kVersion  = 1;
} // namespace

IndexBuilder::IndexBuilder(const DocMeta& meta, std::uint32_t seekInterval)
    : meta_(meta), seekInterval_(seekInterval == 0 ? kDefaultSeekInterval : seekInterval) {}

void IndexBuilder::startChapter(const std::string& title) {
    chapters_.push_back(Chapter{ wordCount_, title });
}

void IndexBuilder::addToken(const std::string& text, std::uint8_t flags) {
    if (wordCount_ % seekInterval_ == 0)
        seekOffsets_.push_back(static_cast<std::uint32_t>(tokenStream_.size()));
    tokenStream_.push_back(flags);
    putBlob16(tokenStream_, text);
    ++wordCount_;
}

std::vector<std::uint8_t> IndexBuilder::finish() {
    std::vector<std::uint8_t> b;
    b.insert(b.end(), kMagic, kMagic + 4);
    putU16(b, kVersion);
    putU16(b, 0); // flags (reserved)
    putU32(b, meta_.sourceSize);
    putU32(b, meta_.sourceMtime);
    putU32(b, wordCount_);
    putU32(b, static_cast<std::uint32_t>(chapters_.size()));
    putU32(b, seekInterval_);
    putBlob16(b, meta_.title);
    putBlob16(b, meta_.author);
    for (const Chapter& c : chapters_) { putU32(b, c.wordOffset); putBlob16(b, c.title); }
    putU32(b, static_cast<std::uint32_t>(seekOffsets_.size()));
    for (std::uint32_t off : seekOffsets_) putU32(b, off);
    b.insert(b.end(), tokenStream_.begin(), tokenStream_.end());
    return b;
}

} // namespace rsvp

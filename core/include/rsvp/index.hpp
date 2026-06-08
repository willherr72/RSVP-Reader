#pragma once
#include "rsvp/token.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace rsvp {

struct DocMeta {
    std::string   title;
    std::string   author;
    std::uint32_t sourceSize  = 0;   // source file size in bytes (cache invalidation)
    std::uint32_t sourceMtime = 0;   // source file mtime (unix seconds)
};

struct Chapter {
    std::uint32_t wordOffset = 0;
    std::string   title;
};

// Default words-per-entry for the sparse seek table.
constexpr std::uint32_t kDefaultSeekInterval = 256;

// Serialize a tokenized document + metadata + chapters into the compiled-index byte
// format. All multi-byte fields are little-endian. seekInterval is the words-per-entry
// of the sparse seek table (0 -> kDefaultSeekInterval).
std::vector<std::uint8_t> serializeIndex(const Document& doc, const DocMeta& meta,
                                         const std::vector<Chapter>& chapters,
                                         std::uint32_t seekInterval = kDefaultSeekInterval);

// A parsed, queryable compiled index. Owns the decoded token stream + tables.
class CompiledIndex {
public:
    // Parse bytes. On bad magic/version/truncation, ok() is false.
    static CompiledIndex parse(const std::vector<std::uint8_t>& bytes);

    bool ok() const { return ok_; }
    const DocMeta& meta() const { return meta_; }
    const std::vector<Chapter>& chapters() const { return chapters_; }
    std::size_t wordCount() const { return wordCount_; }

    Document toDocument() const;                 // reconstruct all tokens
    Token    at(std::size_t wordIndex) const;    // O(seekInterval) random access; {} if out of range

private:
    bool                       ok_ = false;
    DocMeta                    meta_;
    std::vector<Chapter>       chapters_;
    std::size_t                wordCount_ = 0;
    std::uint32_t              seekInterval_ = kDefaultSeekInterval;
    std::vector<std::uint32_t> seekOffsets_;   // byte offsets into the token stream
    std::vector<std::uint8_t>  tokenStream_;   // the token-stream section
};

// Cheap cache-invalidation check: does the index's recorded source stats match?
// Reads only the fixed header; false on bad magic/version/short buffer.
bool indexMatchesSource(const std::vector<std::uint8_t>& bytes,
                        std::uint32_t sourceSize, std::uint32_t sourceMtime);

} // namespace rsvp

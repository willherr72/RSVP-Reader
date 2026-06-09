#pragma once
#include "rsvp/index.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace rsvp {

// Builds the compiled-index byte format incrementally: feed tokens (and chapter
// marks) as they are produced, holding only the serialized stream + seek table,
// never the full Document. Output is byte-identical to serializeIndex.
class IndexBuilder {
public:
    explicit IndexBuilder(const DocMeta& meta, std::uint32_t seekInterval = kDefaultSeekInterval);
    void startChapter(const std::string& title = std::string());  // marks a chapter at the current word
    void addToken(const std::string& text, std::uint8_t flags);
    std::vector<std::uint8_t> finish();

private:
    DocMeta                    meta_;
    std::uint32_t              seekInterval_;
    std::uint32_t              wordCount_ = 0;
    std::vector<std::uint8_t>  tokenStream_;
    std::vector<std::uint32_t> seekOffsets_;
    std::vector<Chapter>       chapters_;
};

} // namespace rsvp

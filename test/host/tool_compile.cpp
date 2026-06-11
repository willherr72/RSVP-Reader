// rsvp_compile: run an EPUB through the real pipeline (epubToIndex) on the host.
// Usage: rsvp_compile <book.epub>  -> prints word count or where it failed.
#include "rsvp/epub.hpp"
#include "rsvp/index.hpp"
#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: rsvp_compile <book.epub>\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { std::fprintf(stderr, "FAIL: cannot open %s\n", argv[1]); return 1; }
    std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::printf("read %zu bytes\n", raw.size());

    std::vector<std::uint8_t> idx = rsvp::epubToIndex(raw, (std::uint32_t)raw.size(), 0);
    if (idx.empty()) { std::fprintf(stderr, "FAIL: epubToIndex returned empty (not a zip / no container/OPF / no spine text)\n"); return 1; }
    std::printf("index: %zu bytes\n", idx.size());

    rsvp::CompiledIndex ci = rsvp::CompiledIndex::parse(idx);
    if (!ci.ok()) { std::fprintf(stderr, "FAIL: compiled index does not parse\n"); return 1; }
    std::printf("OK: %zu words, title '%s'\n", ci.wordCount(), ci.meta().title.c_str());
    if (ci.wordCount() > 0) {
        std::printf("first words:");
        for (std::size_t i = 0; i < 8 && i < ci.wordCount(); i++)
            std::printf(" %s", ci.at(i).text.c_str());
        std::printf("\n");
    }
    return 0;
}

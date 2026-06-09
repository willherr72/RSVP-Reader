#pragma once
#include "rsvp/index.hpp"
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

struct LoadedBook { rsvp::CompiledIndex index; std::string title; };

struct BookEntry {
    std::string   path;        // "/sdcard/Foo.epub"; "" == built-in Sample
    std::string   title;       // from .idx header, else filename
    std::string   author;      // from .idx header, else ""
    std::uint32_t wordCount = 0;
    std::uint32_t position  = 0;   // from .pos, else 0
};

// All books on the card (+ a built-in "Sample" entry, path == "").
std::vector<BookEntry> list_books();
// Load (compile+cache if needed) + parse a specific book. path == "" -> the sample.
std::optional<LoadedBook> load_book(const std::string& path);

void          save_position(const std::string& bookPath, std::uint32_t wordIndex);
std::uint32_t load_position(const std::string& bookPath);   // 0 if none/invalid

// Convenience: load the first SD book (kept for the boot path until boot->menu lands).
std::optional<LoadedBook> load_first_book();

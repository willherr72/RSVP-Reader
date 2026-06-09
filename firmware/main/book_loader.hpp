#pragma once
#include "rsvp/token.hpp"
#include <optional>
#include <string>

struct LoadedBook {
    rsvp::Document doc;
    std::string    title;
    std::string    author;
};

// Scan /sdcard for the first .epub/.txt, load-or-compile its index (cached in
// /sdcard/.rsvp/), and return it. nullopt on no-card/no-book/parse failure.
std::optional<LoadedBook> load_first_book();

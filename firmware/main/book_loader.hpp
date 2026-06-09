#pragma once
#include "rsvp/index.hpp"
#include <optional>
#include <string>

struct LoadedBook {
    rsvp::CompiledIndex index;
    std::string         title;
};

// Scan /sdcard for the first .epub/.txt, load-or-compile its index (cached in
// /sdcard/.rsvp/), and parse it. nullopt on no-card/no-book/failure (the caller
// substitutes the sample).
std::optional<LoadedBook> load_first_book();

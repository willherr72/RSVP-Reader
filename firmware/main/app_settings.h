#pragma once

enum FontSize { FONT_SMALL = 0, FONT_MEDIUM = 1, FONT_LARGE = 2 };

struct Settings {
    int      wpm            = 300;   // 100..800
    FontSize font           = FONT_MEDIUM;
    int      brightness     = 5;     // 1..5
    bool     show_flankers  = true;  // leading/trailing words
    bool     resume_on_open = true;  // open at the saved position vs. at 0%
    bool     start_paused   = false; // open paused (tap to start) vs. auto-play
};

Settings& settings();   // the single in-RAM instance
void settings_load();    // from NVS (namespace "rsvp"); defaults if missing
void settings_save();    // persist current settings()

#ifndef UI_READER_H
#define UI_READER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Show a "Loading..." screen and start compiling/parsing the book in a background
// task. Call once at startup under the LVGL lock.
void rsvp_loading_screen_create(void);

// True once the background load has finished (book or sample is ready in g_index).
bool rsvp_book_ready(void);

// Drop the Loading screen and build the reader from the loaded index. Call from the
// main task under the LVGL lock once rsvp_book_ready() returns true.
void rsvp_build_reader_screen(void);

// Open a specific book by path ("" = sample): loading screen -> background load -> reader,
// resuming from .pos when enabled. Call from the LVGL thread.
void rsvp_open_book_path(const char* path);

// Persist the open book's current word index to its .pos (pause / leaving to the menu).
void rsvp_reader_save_position(void);

// Pause the reader if it's playing (e.g. when the menu opens).
void rsvp_reader_pause(void);

// Re-apply persisted settings (wpm, flankers) to the running reader after the Settings
// screen changes them. Font size applies on the next book open.
void rsvp_reader_apply_settings(void);

#ifdef __cplusplus
}
#endif

#endif  // UI_READER_H

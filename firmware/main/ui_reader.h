#ifndef UI_READER_H
#define UI_READER_H

#ifdef __cplusplus
extern "C" {
#endif

// Show a "Loading..." screen and compile/parse the book in a background task; the
// reader screen is built automatically once the book is ready (or the sample, on
// failure). Call once at startup under the LVGL lock.
void rsvp_loading_screen_create(void);

#ifdef __cplusplus
}
#endif

#endif  // UI_READER_H

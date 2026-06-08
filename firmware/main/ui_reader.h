#ifndef UI_READER_H
#define UI_READER_H

#ifdef __cplusplus
extern "C" {
#endif

// Build the RSVP reading screen on the active LVGL screen (static mockup for now:
// word with red ORP letter, focal ticks, flankers, status). Driven by the engine later.
void rsvp_reading_screen_create(void);

#ifdef __cplusplus
}
#endif

#endif  // UI_READER_H

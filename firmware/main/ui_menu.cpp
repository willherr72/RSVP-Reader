#include "ui_menu.h"
#include "ui_reader.h"
#include "book_loader.hpp"
#include "app_settings.h"
#include "power_bsp.h"

#include "lvgl.h"
#include "esp_log.h"
#include <atomic>
#include <vector>
#include <cstdio>

namespace {

enum Screen { SCR_READER, SCR_MENU, SCR_LIBRARY, SCR_SETTINGS, SCR_WIFI };

// Task 6 interim values: boot goes to the reader, so a book is already open.
// Task 9 flips these to { SCR_MENU, false } (boot -> menu, nothing open yet).
Screen g_screen   = SCR_READER;
bool   g_book_open = true;

std::atomic<bool> g_boot_pressed{false};
lv_obj_t* g_overlay = nullptr;   // current menu/library/settings overlay (null while in the reader)

void close_overlay() { if (g_overlay) { lv_obj_del(g_overlay); g_overlay = nullptr; } }

// Full-screen opaque panel over whatever is beneath (the reader stays alive underneath).
lv_obj_t* make_overlay() {
    lv_obj_t* o = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(o, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

lv_obj_t* centered_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(0xf2f5fa), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    return l;
}

void show_menu();
void show_library();
void show_settings();

void tile_cb(lv_event_t* e) {
    Screen which = (Screen)(intptr_t)lv_event_get_user_data(e);
    close_overlay();
    if (which == SCR_LIBRARY)       show_library();
    else if (which == SCR_SETTINGS) show_settings();
    else {
        g_screen = SCR_WIFI;
        g_overlay = make_overlay();
        centered_label(g_overlay, "WiFi Drop\n(coming soon)");
    }
}

void show_menu() {
    g_screen = SCR_MENU;
    g_overlay = make_overlay();
    lv_obj_set_flex_flow(g_overlay, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_overlay, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(g_overlay, 8, 0);
    lv_obj_set_style_pad_column(g_overlay, 8, 0);

    static const char* names[]   = { "Library", "Settings", "WiFi Drop" };
    static const Screen targets[] = { SCR_LIBRARY, SCR_SETTINGS, SCR_WIFI };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* t = lv_obj_create(g_overlay);
        lv_obj_remove_style_all(t);
        lv_obj_set_flex_grow(t, 1);
        lv_obj_set_height(t, LV_PCT(100));
        lv_obj_set_style_bg_color(t, lv_color_hex(0x15171c), 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(t, 8, 0);
        lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(t, tile_cb, LV_EVENT_CLICKED, (void*)(intptr_t)targets[i]);
        centered_label(t, names[i]);
    }
}

// Tap a Library row -> open that book in the reader (resuming via .pos).
void open_selected(lv_event_t* e) {
    const char* path = (const char*)lv_event_get_user_data(e);
    close_overlay();
    g_book_open = true;
    g_screen = SCR_READER;
    rsvp_open_book_path(path);     // loading screen -> background load -> build_reader
}

void show_library() {
    g_screen = SCR_LIBRARY;
    g_overlay = make_overlay();
    lv_obj_set_flex_flow(g_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_overlay, LV_DIR_VER);
    lv_obj_set_style_pad_all(g_overlay, 0, 0);
    lv_obj_set_style_pad_row(g_overlay, 0, 0);

    // static so the BookEntry strings outlive the screen (the row callbacks point at b.path).
    static std::vector<BookEntry> books;
    books = list_books();
    for (auto& b : books) {
        lv_obj_t* row = lv_obj_create(g_overlay);
        lv_obj_remove_style_all(row);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 52);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x121419), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, open_selected, LV_EVENT_CLICKED, (void*)b.path.c_str());

        lv_obj_t* title = lv_label_create(row);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_width(title, 470);
        lv_label_set_text(title, b.title.c_str());
        lv_obj_set_style_text_color(title, lv_color_hex(0xf2f5fa), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 5);

        lv_obj_t* auth = lv_label_create(row);
        lv_label_set_text(auth, b.author.c_str());
        lv_obj_set_style_text_color(auth, lv_color_hex(0x8893a6), 0);
        lv_obj_align(auth, LV_ALIGN_BOTTOM_LEFT, 16, -6);

        const int pct = (b.wordCount > 0) ? (int)((uint64_t)b.position * 100 / b.wordCount) : 0;
        char buf[8]; std::snprintf(buf, sizeof buf, "%d%%", pct);
        lv_obj_t* pc = lv_label_create(row);
        lv_label_set_text(pc, buf);
        lv_obj_set_style_text_color(pc, lv_color_hex(0xcdd6e6), 0);
        lv_obj_set_style_text_font(pc, &lv_font_montserrat_16, 0);
        lv_obj_align(pc, LV_ALIGN_RIGHT_MID, -16, 0);

        lv_obj_t* bar = lv_obj_create(row);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, LV_PCT(pct), 3);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xff3b3b), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
}

// Stub until Task 8 fills it in.
void show_settings() { g_screen = SCR_SETTINGS; g_overlay = make_overlay(); centered_label(g_overlay, "Settings\n(coming soon)"); }

void on_boot() { g_boot_pressed.store(true); }   // from the power_bsp button task

// LVGL thread: react to a BOOT press recorded by on_boot().
void nav_timer_cb(lv_timer_t*) {
    if (!g_boot_pressed.exchange(false)) return;
    switch (g_screen) {
        case SCR_READER:
            rsvp_reader_save_position();
            rsvp_reader_pause();
            show_menu();                       // overlay on top of the (paused) reader
            break;
        case SCR_MENU:
            if (g_book_open) { close_overlay(); g_screen = SCR_READER; }
            break;
        case SCR_LIBRARY:
        case SCR_SETTINGS:
        case SCR_WIFI:
            close_overlay();
            show_menu();
            break;
    }
}

} // namespace

extern "C" void ui_menu_open(void) { close_overlay(); show_menu(); }

extern "C" void ui_menu_init(void) {
    power_bsp_set_boot_cb(on_boot);
    lv_timer_create(nav_timer_cb, 80, nullptr);
}

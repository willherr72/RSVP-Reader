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

// Stubs until Tasks 7 / 8 fill them in.
void show_library()  { g_screen = SCR_LIBRARY;  g_overlay = make_overlay(); centered_label(g_overlay, "Library\n(coming soon)"); }
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

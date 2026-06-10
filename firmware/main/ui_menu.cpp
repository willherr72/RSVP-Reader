#include "ui_menu.h"
#include "ui_reader.h"
#include "book_loader.hpp"
#include "app_settings.h"
#include "power_bsp.h"
#include "lcd_bl_pwm_bsp.h"
#include "touch_cal.h"
#include "rtc_bsp.hpp"
#include "imu_bsp.hpp"
#include "autorotate.h"
#include "battery_bsp.h"

#include "lvgl.h"
#include "esp_log.h"
#include <atomic>
#include <vector>
#include <cstdio>

namespace {

enum Screen { SCR_READER, SCR_MENU, SCR_LIBRARY, SCR_SETTINGS, SCR_WIFI, SCR_CALIB, SCR_SETCLOCK };

// Boot goes to the menu; no book is open until one is picked from the Library.
Screen g_screen   = SCR_MENU;
bool   g_book_open = false;

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
    lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);   // tiles fill it; don't eat taps as scrolls
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

        const int pct = (b.wordCount > 0)
            ? (int)(((uint64_t)b.position * 100 + b.wordCount / 2) / b.wordCount) : 0;   // round (match the reader)
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

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

enum StepKind { STEP_WPM, STEP_FONT, STEP_BRI };
struct StepCtx { StepKind kind; int delta; lv_obj_t* lbl; };
StepCtx s_steps[6];   // [0,1]=speed -/+, [2,3]=font, [4,5]=brightness

void step_text(StepKind k, char* buf, std::size_t n) {
    switch (k) {
        case STEP_WPM:  std::snprintf(buf, n, "%d wpm", settings().wpm); break;
        case STEP_FONT: { static const char* fn[] = {"Small","Medium","Large"};
                          std::snprintf(buf, n, "%s", fn[settings().font]); break; }
        case STEP_BRI:  std::snprintf(buf, n, "%d", settings().brightness); break;
    }
}

void step_apply(StepKind k) {
    settings_save();
    if (k == STEP_BRI) setUpduty((uint16_t)((5 - settings().brightness) * 40));   // inverted: 5->duty0 (bright), 1->duty160 (dim)
    else               rsvp_reader_apply_settings();   // wpm now; font applies on next open
}

void step_cb(lv_event_t* e) {
    StepCtx* c = (StepCtx*)lv_event_get_user_data(e);
    switch (c->kind) {
        case STEP_WPM:  settings().wpm = clampi(settings().wpm + c->delta * 25, 100, 800); break;
        case STEP_FONT: settings().font = (FontSize)clampi((int)settings().font + c->delta, 0, 2); break;
        case STEP_BRI:  settings().brightness = clampi(settings().brightness + c->delta, 1, 5); break;
    }
    char buf[16]; step_text(c->kind, buf, sizeof buf);
    lv_label_set_text(c->lbl, buf);
    step_apply(c->kind);
}

void switch_cb(lv_event_t* e) {
    intptr_t which = (intptr_t)lv_event_get_user_data(e);
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (which == 0)      { settings().show_flankers = on; settings_save(); rsvp_reader_apply_settings(); }
    else if (which == 1) { settings().resume_on_open = on; settings_save(); }
    else if (which == 2) { settings().start_paused = on; settings_save(); }
    else                 { settings().auto_rotate = on; settings_save(); }
}

lv_obj_t* settings_row(const char* name) {
    lv_obj_t* row = lv_obj_create(g_overlay);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 50);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x121419), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_t* l = lv_label_create(row);
    lv_label_set_text(l, name);
    lv_obj_set_style_text_color(l, lv_color_hex(0xf2f5fa), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 16, 0);
    return row;
}

lv_obj_t* step_btn(lv_obj_t* parent, const char* sym, StepCtx* ctx) {
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_height(b, 46);
    lv_obj_set_flex_grow(b, 1);            // fill the cluster -> wide, easy-to-hit targets
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x4a525f), 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, step_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_color(l, lv_color_hex(0xcdd6e6), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_center(l);
    return b;
}

void add_stepper(const char* name, StepKind kind, int idx) {
    lv_obj_t* row = settings_row(name);
    lv_obj_t* cluster = lv_obj_create(row);
    lv_obj_remove_style_all(cluster);
    lv_obj_clear_flag(cluster, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cluster, 360, LV_PCT(100));
    lv_obj_align(cluster, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_flex_flow(cluster, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cluster, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cluster, 10, 0);

    s_steps[idx]     = StepCtx{ kind, -1, nullptr };
    s_steps[idx + 1] = StepCtx{ kind, +1, nullptr };
    step_btn(cluster, "-", &s_steps[idx]);
    lv_obj_t* val = lv_label_create(cluster);
    char buf[16]; step_text(kind, buf, sizeof buf);
    lv_label_set_text(val, buf);
    lv_obj_set_style_text_color(val, lv_color_hex(0xf2f5fa), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
    lv_obj_set_width(val, 92);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    step_btn(cluster, "+", &s_steps[idx + 1]);
    s_steps[idx].lbl     = val;
    s_steps[idx + 1].lbl = val;
}

void add_switch(const char* name, int which, bool on) {
    lv_obj_t* row = settings_row(name);
    lv_obj_t* sw = lv_switch_create(row);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0xff3b3b), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_add_event_cb(sw, switch_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)which);
}

// --- touch calibration (re-runnable from Settings) ---
const int CAL_TX[3] = {80, 560, 320};   // target logical positions
const int CAL_TY[3] = {35, 35, 150};
int g_cal_step = 0;
uint16_t g_cal_raw[3][2];

void draw_cross_at(lv_obj_t* parent, int x, int y) {
    lv_obj_t* h = lv_obj_create(parent); lv_obj_remove_style_all(h);
    lv_obj_set_size(h, 40, 3); lv_obj_set_style_bg_color(h, lv_color_hex(0xff3b3b), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0); lv_obj_set_pos(h, x - 20, y - 1);
    lv_obj_t* v = lv_obj_create(parent); lv_obj_remove_style_all(v);
    lv_obj_set_size(v, 3, 40); lv_obj_set_style_bg_color(v, lv_color_hex(0xff3b3b), 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0); lv_obj_set_pos(v, x - 1, y - 20);
}

void draw_cal_step() {
    lv_obj_clean(g_overlay);
    draw_cross_at(g_overlay, CAL_TX[g_cal_step], CAL_TY[g_cal_step]);
    lv_obj_t* l = lv_label_create(g_overlay);
    char buf[40]; std::snprintf(buf, sizeof buf, "Tap the cross  (%d/3)", g_cal_step + 1);
    lv_label_set_text(l, buf);
    lv_obj_set_style_text_color(l, lv_color_hex(0x8893a6), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
}

// Solve t_i = out[0]*rx_i + out[1]*ry_i + out[2] for the 3 calibration points (Cramer).
bool solve3(const double rx[3], const double ry[3], const double t[3], double out[3]) {
    double det = rx[0]*(ry[1]-ry[2]) - ry[0]*(rx[1]-rx[2]) + (rx[1]*ry[2]-rx[2]*ry[1]);
    if (det < 1e-6 && det > -1e-6) return false;
    out[0] = (t[0]*(ry[1]-ry[2]) - ry[0]*(t[1]-t[2]) + (t[1]*ry[2]-t[2]*ry[1])) / det;
    out[1] = (rx[0]*(t[1]-t[2]) - t[0]*(rx[1]-rx[2]) + (rx[1]*t[2]-rx[2]*t[1])) / det;
    out[2] = (rx[0]*(ry[1]*t[2]-ry[2]*t[1]) - ry[0]*(rx[1]*t[2]-rx[2]*t[1]) + t[0]*(rx[1]*ry[2]-rx[2]*ry[1])) / det;
    return true;
}

void finish_calib_async(void*) { close_overlay(); show_menu(); }

void cal_pressed_cb(lv_event_t*) {
    if (g_cal_step < 3) touch_get_raw(&g_cal_raw[g_cal_step][0], &g_cal_raw[g_cal_step][1]);
}

void cal_released_cb(lv_event_t*) {
    if (g_cal_step >= 3) return;
    g_cal_step++;
    if (g_cal_step < 3) { draw_cal_step(); return; }
    double rx[3], ry[3], tx[3], ty[3], cx[3], cy[3];
    for (int i = 0; i < 3; i++) { rx[i] = g_cal_raw[i][0]; ry[i] = g_cal_raw[i][1];
                                  tx[i] = CAL_TX[i]; ty[i] = CAL_TY[i]; }
    if (solve3(rx, ry, tx, cx) && solve3(rx, ry, ty, cy)) {
        float coef[6] = { (float)cx[0], (float)cx[1], (float)cx[2],
                          (float)cy[0], (float)cy[1], (float)cy[2] };
        touch_set_calibration(coef);
    }
    lv_async_call(finish_calib_async, nullptr);   // defer the screen change out of the event
}

void start_calibration() {
    close_overlay();
    g_screen = SCR_CALIB;
    g_cal_step = 0;
    g_overlay = make_overlay();
    lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_overlay, cal_pressed_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(g_overlay, cal_released_cb, LV_EVENT_RELEASED, nullptr);
    draw_cal_step();
}

void cal_entry_cb(lv_event_t*) { start_calibration(); }

void add_action(const char* name, lv_event_cb_t cb) {
    lv_obj_t* row = settings_row(name);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* chev = lv_label_create(row);
    lv_label_set_text(chev, ">");
    lv_obj_set_style_text_color(chev, lv_color_hex(0x8893a6), 0);
    lv_obj_set_style_text_font(chev, &lv_font_montserrat_16, 0);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -16, 0);
}

// --- Set clock screen ---
int  g_set_h = 12;     // 1..12
int  g_set_m = 0;      // 0..59
bool g_set_pm = false;
lv_obj_t* g_set_h_lbl = nullptr;
lv_obj_t* g_set_m_lbl = nullptr;
lv_obj_t* g_set_ap_lbl = nullptr;

void set_refresh() {
    char b[8];
    std::snprintf(b, sizeof b, "%d", g_set_h);   lv_label_set_text(g_set_h_lbl, b);
    std::snprintf(b, sizeof b, "%02d", g_set_m);  lv_label_set_text(g_set_m_lbl, b);
    lv_label_set_text(g_set_ap_lbl, g_set_pm ? "PM" : "AM");
}
void set_h_cb(lv_event_t* e) { int d = (int)(intptr_t)lv_event_get_user_data(e);
    g_set_h += d; if (g_set_h < 1) g_set_h = 12; if (g_set_h > 12) g_set_h = 1; set_refresh(); }
void set_m_cb(lv_event_t* e) { int d = (int)(intptr_t)lv_event_get_user_data(e);
    g_set_m = (g_set_m + d + 60) % 60; set_refresh(); }
void set_ap_cb(lv_event_t*) { g_set_pm = !g_set_pm; set_refresh(); }
void set_go_cb(lv_event_t*) {
    rsvp::RtcTime t{ rsvp::to24h((std::uint8_t)g_set_h, g_set_pm), (std::uint8_t)g_set_m, 0 };
    rtc_set(t);
    close_overlay(); show_menu();
}

// A labelled stepper row that drives a callback (delta passed as user_data).
lv_obj_t* clock_row(const char* name, lv_event_cb_t cb, lv_obj_t** value_out) {
    lv_obj_t* row = settings_row(name);
    lv_obj_t* cluster = lv_obj_create(row);
    lv_obj_remove_style_all(cluster); lv_obj_clear_flag(cluster, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cluster, 300, LV_PCT(100)); lv_obj_align(cluster, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_flex_flow(cluster, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cluster, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cluster, 8, 0);
    auto mkbtn = [&](const char* sym, int delta) {
        lv_obj_t* b = lv_obj_create(cluster); lv_obj_remove_style_all(b);
        lv_obj_set_height(b, 46); lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_border_width(b, 1, 0); lv_obj_set_style_border_color(b, lv_color_hex(0x4a525f), 0);
        lv_obj_set_style_radius(b, 8, 0); lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)(intptr_t)delta);
        lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, sym);
        lv_obj_set_style_text_color(l, lv_color_hex(0xcdd6e6), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0); lv_obj_center(l);
    };
    mkbtn("-", -1);
    lv_obj_t* v = lv_label_create(cluster); lv_obj_set_width(v, 70);
    lv_obj_set_style_text_color(v, lv_color_hex(0xf2f5fa), 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    mkbtn("+", +1);
    *value_out = v;
    return row;
}

void show_setclock() {
    g_screen = SCR_SETCLOCK;
    g_overlay = make_overlay();
    lv_obj_set_flex_flow(g_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_overlay, LV_DIR_VER);
    lv_obj_set_style_pad_all(g_overlay, 0, 0); lv_obj_set_style_pad_row(g_overlay, 0, 0);
    rsvp::RtcTime t;                               // pre-fill from the RTC, else 12:00 AM
    if (rtc_valid() && rtc_get(t)) {
        int h = t.hour % 12; g_set_h = (h == 0) ? 12 : h;
        g_set_m = t.minute; g_set_pm = t.hour >= 12;
    } else { g_set_h = 12; g_set_m = 0; g_set_pm = false; }
    clock_row("Hour",   set_h_cb, &g_set_h_lbl);
    clock_row("Minute", set_m_cb, &g_set_m_lbl);
    // AM/PM row
    lv_obj_t* ap = settings_row("AM / PM");
    lv_obj_t* apb = lv_obj_create(ap); lv_obj_remove_style_all(apb);
    lv_obj_set_size(apb, 90, 44); lv_obj_align(apb, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_border_width(apb, 1, 0); lv_obj_set_style_border_color(apb, lv_color_hex(0x4a525f), 0);
    lv_obj_set_style_radius(apb, 8, 0); lv_obj_add_flag(apb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(apb, set_ap_cb, LV_EVENT_CLICKED, nullptr);
    g_set_ap_lbl = lv_label_create(apb);
    lv_obj_set_style_text_color(g_set_ap_lbl, lv_color_hex(0xf2f5fa), 0);
    lv_obj_set_style_text_font(g_set_ap_lbl, &lv_font_montserrat_16, 0); lv_obj_center(g_set_ap_lbl);
    // Set button
    lv_obj_t* setrow = settings_row("");
    lv_obj_t* sb = lv_obj_create(setrow); lv_obj_remove_style_all(sb);
    lv_obj_set_size(sb, LV_PCT(92), 44); lv_obj_center(sb);
    lv_obj_set_style_bg_color(sb, lv_color_hex(0xff3b3b), 0); lv_obj_set_style_bg_opa(sb, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sb, 8, 0); lv_obj_add_flag(sb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sb, set_go_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* sl = lv_label_create(sb); lv_label_set_text(sl, "Set");
    lv_obj_set_style_text_color(sl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_16, 0); lv_obj_center(sl);
    set_refresh();
}

void setclock_entry_cb(lv_event_t*) { close_overlay(); show_setclock(); }

void show_settings() {
    g_screen = SCR_SETTINGS;
    g_overlay = make_overlay();
    lv_obj_set_flex_flow(g_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_overlay, LV_DIR_VER);
    lv_obj_set_style_pad_all(g_overlay, 0, 0);
    lv_obj_set_style_pad_row(g_overlay, 0, 0);
    add_stepper("Reading speed", STEP_WPM, 0);
    add_stepper("Font size", STEP_FONT, 2);
    add_stepper("Brightness", STEP_BRI, 4);
    add_switch("Leading / trailing words", 0, settings().show_flankers);
    add_switch("Resume position", 1, settings().resume_on_open);
    add_switch("Start paused", 2, settings().start_paused);
    add_switch("Auto-rotate", 3, settings().auto_rotate);
    add_action("Calibrate touch", cal_entry_cb);
    add_action("Set clock", setclock_entry_cb);
}

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
        case SCR_CALIB:
        case SCR_SETCLOCK:
            close_overlay();
            show_menu();
            break;
    }
}

} // namespace

extern "C" void ui_menu_open(void) { close_overlay(); show_menu(); }

extern "C" void ui_menu_init(void) {
    settings_load();                  // load persisted settings (and init NVS) at boot
    touch_cal_load();                 // load persisted touch calibration
    rtc_init();
    imu_init();
    batt_init();
    autorotate_start();
    setUpduty((uint16_t)((5 - settings().brightness) * 40));   // apply saved brightness (duty is inverted)
    power_bsp_set_boot_cb(on_boot);
    lv_timer_create(nav_timer_cb, 80, nullptr);
}

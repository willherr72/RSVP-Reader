#include "ui_reader.h"

#include "lvgl.h"

#include "rsvp/tokenize.hpp"
#include "rsvp/player.hpp"
#include "rsvp/orp.hpp"
#include "rsvp/pacing.hpp"
#include "rsvp/gesture.hpp"
#include "rsvp/indexbuilder.hpp"

#include "book_loader.hpp"
#include "power_bsp.h"
#include "app_settings.h"
#include "rtc_bsp.hpp"
#include "battery_bsp.h"
#include "rsvp/battery.hpp"
#include "rsvp/eta.hpp"
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <cstdio>
#include <atomic>

using namespace rsvp;

namespace {

CompiledIndex g_index;
Player*     g_player  = nullptr;
int         g_wpm     = 300;
std::size_t g_lastIdx = static_cast<std::size_t>(-1);

// Book-open state: the boot path loads the first SD book; the library opens a path.
std::string g_pending_path;
bool        g_use_first = true;
std::string g_book_path;          // currently-open book (for .pos resume/save)
lv_timer_t* g_tick_timer = nullptr;

static const lv_font_t* font_for(FontSize f) {
    switch (f) {
        case FONT_SMALL: return &lv_font_montserrat_32;
        case FONT_LARGE: return &lv_font_montserrat_48;
        default:         return &lv_font_montserrat_40;   // Medium
    }
}

lv_obj_t* g_clk = nullptr;     // the reader's clock label (null when no reader is up)
lv_obj_t* g_batt = nullptr;    // battery % label (in the top-right cluster with the clock)

void clock_update() {
    if (g_clk == nullptr) return;
    rsvp::RtcTime t;
    if (rtc_valid() && rtc_get(t)) lv_label_set_text(g_clk, formatClock12h(t.hour, t.minute).c_str());
    else                           lv_label_set_text(g_clk, "--:--");
}
void batt_update() {
    if (g_batt == nullptr) return;
    int mv = batt_read_mv();
    if (mv > 0) { char b[8]; std::snprintf(b, sizeof b, "%d%%", batteryPercent(mv)); lv_label_set_text(g_batt, b); }
    else        lv_label_set_text(g_batt, "");
}
void clock_timer_cb(lv_timer_t*) { clock_update(); batt_update(); }

lv_obj_t *g_scr      = nullptr;
lv_obj_t *g_pre      = nullptr;
lv_obj_t *g_orp      = nullptr;
lv_obj_t *g_post     = nullptr;
lv_obj_t *g_prev     = nullptr;
lv_obj_t *g_next     = nullptr;
lv_obj_t *g_wpm_lbl  = nullptr;
lv_obj_t *g_eta      = nullptr;     // bottom-right time-to-finish label
lv_obj_t *g_tick_top = nullptr;
lv_obj_t *g_tick_bot = nullptr;

lv_point_t g_press_pt = {0, 0};

// The persistent load task (created once at boot) waits on g_load_request, loads the
// book off the LVGL thread, then sets g_load_done (polled by an lv_timer).
std::atomic<bool> g_load_done{false};
std::atomic<bool> g_load_request{false};
std::string       g_loaded_title;
lv_obj_t*         g_loading_scr = nullptr;

// Power-off: the power_bsp button task flips this flag (it must not touch LVGL); an
// lv_timer on the LVGL thread shows "Powering off..." then cuts power.
std::atomic<bool> g_shutdown_requested{false};
void on_shutdown_requested() { g_shutdown_requested.store(true); }

void shutdown_timer_cb(lv_timer_t* t)
{
    (void)t;
    if (!g_shutdown_requested.load()) return;
    g_shutdown_requested.store(false);

    lv_obj_t* o = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(o, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_t* lbl = lv_label_create(o);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xf2f5fa), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
    lv_label_set_text(lbl, "Powering off...");
    lv_obj_center(lbl);
    lv_refr_now(NULL);

    power_off();                        // P6 low -> on battery the board dies here
    vTaskDelay(pdMS_TO_TICKS(2000));    // still alive after this -> externally (USB) powered

    // Survived the power-off: re-hold power and return to the reader (don't get stuck).
    power_hold();
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_label_set_text(lbl, "On USB - unplug to power off");
    lv_obj_center(lbl);
    lv_refr_now(NULL);
    vTaskDelay(pdMS_TO_TICKS(1500));
    lv_obj_del(o);                      // dismiss; the reader underneath resumes
}

// Bottom status line: pause glyph (when paused) + wpm + progress %.
void update_status()
{
    if (g_player == nullptr) return;
    char buf[48];
    const char *sym = g_player->isPlaying() ? "" : LV_SYMBOL_PAUSE "  ";
    std::snprintf(buf, sizeof(buf), "%s%d wpm  .  %d%%", sym, g_wpm,
                  static_cast<int>(g_player->progress() * 100.0 + 0.5));
    lv_label_set_text(g_wpm_lbl, buf);
    if (g_eta) {
        if (settings().show_eta) {
            int rem = (int)g_index.wordCount() - (int)g_player->index();
            lv_label_set_text(g_eta, etaString(rem, g_wpm).c_str());
        } else {
            lv_label_set_text(g_eta, "");
        }
    }
}

// Re-render the current word: split at the ORP, pin the red letter to screen centre,
// flank pre/post + prev/next words, and reposition the focal ticks over the ORP letter.
void refresh_word()
{
    if (g_player == nullptr || g_index.wordCount() == 0) return;
    const std::size_t idx = g_player->index();

    const OrpSplit s = orpSplit(g_player->current().text);
    lv_label_set_text(g_pre,  s.pre.c_str());
    lv_label_set_text(g_orp,  s.orp.c_str());
    lv_label_set_text(g_post, s.post.c_str());

    // at() returns a Token by value; bind to a local before .c_str() to avoid dangling.
    const Token prev = (idx > 0) ? g_index.at(idx - 1) : Token{};
    const Token next = (idx + 1 < g_index.wordCount()) ? g_index.at(idx + 1) : Token{};
    lv_label_set_text(g_prev, prev.text.c_str());
    lv_label_set_text(g_next, next.text.c_str());

    lv_obj_align(g_orp, LV_ALIGN_CENTER, 0, 0);
    lv_obj_update_layout(g_scr);
    lv_obj_align_to(g_pre,      g_orp, LV_ALIGN_OUT_LEFT_MID,   0, 0);
    lv_obj_align_to(g_post,     g_orp, LV_ALIGN_OUT_RIGHT_MID,  0, 0);
    lv_obj_align_to(g_tick_top, g_orp, LV_ALIGN_OUT_TOP_MID,    0, -10);
    lv_obj_align_to(g_tick_bot, g_orp, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    update_status();
}

void set_wpm(int w)
{
    if (w < 100) w = 100;
    if (w > 800) w = 800;
    g_wpm = w;
    settings().wpm = g_wpm;          // persist the speed (swipe up/down saves too)
    settings_save();
    PacingConfig cfg = g_player->config();
    cfg.wpm = g_wpm;
    g_player->setConfig(cfg);
    update_status();
}

void do_next_sentence()
{
    g_player->nextSentence();
    g_lastIdx = g_player->index();
    refresh_word();
}

void do_prev_sentence()
{
    g_player->prevSentence();
    g_lastIdx = g_player->index();
    refresh_word();
}

// Tap = play/pause, swipe up/down = WPM +/-25, swipe left/right = sentence.
// Press records the start point; release classifies the delta.
void touch_event_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr || g_player == nullptr) return;
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &g_press_pt);
        return;
    }
    if (code != LV_EVENT_RELEASED) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const int dx = static_cast<int>(p.x) - static_cast<int>(g_press_pt.x);
    const int dy = static_cast<int>(p.y) - static_cast<int>(g_press_pt.y);
    const Gesture g = classifyGesture(dx, dy, 25);

    switch (g) {
        case Gesture::Tap:
            g_player->togglePlay();
            update_status();
            if (!g_player->isPlaying()) rsvp_reader_save_position();
            break;
        case Gesture::SwipeUp:    set_wpm(g_wpm + 25); break;   // up = faster
        case Gesture::SwipeDown:  set_wpm(g_wpm - 25); break;
        // Touch X is screen-mirrored vs. the held device (see firmware-notes), so a
        // physical left-swipe classifies as SwipeRight. Map so physical left = previous.
        case Gesture::SwipeLeft:  do_next_sentence(); break;
        case Gesture::SwipeRight: do_prev_sentence(); break;
        case Gesture::None:       break;
    }
}

void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (g_player == nullptr) return;
    g_player->tick(33);              // nominal LVGL timer period (ms)
    if (g_player->isFinished()) {    // loop the sample so it reads continuously
        g_player->seek(0);
        g_player->play();
    }
    if (g_player->index() != g_lastIdx) {
        g_lastIdx = g_player->index();
        refresh_word();
    }
}

lv_obj_t *make_label(lv_obj_t *parent, lv_color_t color, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

}  // namespace

// Build the reader screen from the already-loaded g_index (populated by load_task).
static void build_reader(const std::string& book_title)
{
    // Tear down any previous reader (re-entrant: opening another book rebuilds this).
    if (g_tick_timer) { lv_timer_del(g_tick_timer); g_tick_timer = nullptr; }
    if (g_player)     { delete g_player; g_player = nullptr; }
    g_clk = nullptr;               // about to be deleted by lv_obj_clean below
    g_batt = nullptr;
    g_scr = lv_screen_active();
    lv_obj_clean(g_scr);            // remove the loading overlay / any previous reader objects
    g_loading_scr = nullptr;        // (was a child of g_scr, now deleted)
    g_wpm = settings().wpm;         // apply the persisted reading speed
    lv_obj_set_style_bg_color(g_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);

    const lv_color_t dim   = lv_color_hex(0x8893a6);
    const lv_color_t faint = lv_color_hex(0x39414f);
    const lv_color_t white = lv_color_hex(0xf2f5fa);
    const lv_color_t red   = lv_color_hex(0xff3b3b);

    // top status
    lv_obj_t *batt = make_label(g_scr, dim, &lv_font_montserrat_16);
    lv_label_set_long_mode(batt, LV_LABEL_LONG_DOT);
    lv_obj_set_width(batt, 360);
    lv_label_set_text(batt, book_title.c_str());
    lv_obj_align(batt, LV_ALIGN_TOP_LEFT, 10, 6);

    lv_obj_t* topr = lv_obj_create(g_scr);
    lv_obj_remove_style_all(topr);
    lv_obj_set_size(topr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(topr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topr, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(topr, 10, 0);
    lv_obj_align(topr, LV_ALIGN_TOP_RIGHT, -10, 6);
    g_batt = make_label(topr, dim, &lv_font_montserrat_16);
    g_clk  = make_label(topr, dim, &lv_font_montserrat_16);
    clock_update();                 // show time + battery immediately
    batt_update();

    // faint flankers (previous / next word)
    g_prev = make_label(g_scr, faint, &lv_font_montserrat_16);
    lv_obj_align(g_prev, LV_ALIGN_LEFT_MID, 18, 0);
    g_next = make_label(g_scr, faint, &lv_font_montserrat_16);
    lv_obj_align(g_next, LV_ALIGN_RIGHT_MID, -18, 0);
    if (!settings().show_flankers) {
        lv_obj_add_flag(g_prev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_next, LV_OBJ_FLAG_HIDDEN);
    }

    // the word: pre + ORP(red) + post, ORP pinned to screen centre
    const lv_font_t* word_font = font_for(settings().font);
    g_pre  = make_label(g_scr, white, word_font);
    g_orp  = make_label(g_scr, red,   word_font);
    g_post = make_label(g_scr, white, word_font);

    // focal ticks
    static lv_style_t tick;
    static bool tick_inited = false;
    if (!tick_inited) {
        lv_style_init(&tick);
        lv_style_set_bg_color(&tick, lv_color_hex(0xcdd6e6));
        lv_style_set_bg_opa(&tick, LV_OPA_40);
        tick_inited = true;
    }

    g_tick_top = lv_obj_create(g_scr);
    lv_obj_remove_style_all(g_tick_top);
    lv_obj_add_style(g_tick_top, &tick, 0);
    lv_obj_set_size(g_tick_top, 2, 20);

    g_tick_bot = lv_obj_create(g_scr);
    lv_obj_remove_style_all(g_tick_bot);
    lv_obj_add_style(g_tick_bot, &tick, 0);
    lv_obj_set_size(g_tick_bot, 2, 20);

    // bottom wpm . %
    g_wpm_lbl = make_label(g_scr, dim, &lv_font_montserrat_16);
    lv_obj_align(g_wpm_lbl, LV_ALIGN_BOTTOM_MID, 0, -6);
    g_eta = make_label(g_scr, dim, &lv_font_montserrat_16);
    lv_obj_align(g_eta, LV_ALIGN_BOTTOM_RIGHT, -10, -6);

    // --- engine: drive a fresh Player over the loaded document ---
    PacingConfig cfg;
    cfg.wpm = g_wpm;
    g_player = new Player(g_index, cfg);
    g_book_path = g_pending_path;
    if (settings().resume_on_open) {
        const std::uint32_t p = load_position(g_book_path);
        if (p < g_index.wordCount()) g_player->seek(p);
    }
    g_lastIdx = static_cast<std::size_t>(-1);
    if (!settings().start_paused) g_player->play();   // else open paused; tap to start

    refresh_word();
    g_tick_timer = lv_timer_create(tick_cb, 33, nullptr);

    // transparent full-screen touch layer on top: tap / swipe via press-release delta
    lv_obj_t *touch = lv_obj_create(g_scr);
    lv_obj_remove_style_all(touch);
    lv_obj_set_size(touch, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(touch, touch_event_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(touch, touch_event_cb, LV_EVENT_RELEASED, nullptr);
}

namespace {

// Background (its own big stack, off the LVGL/main thread): scan + compile/parse the
// first SD book into g_index, or fall back to the built-in sample. Never touches LVGL;
// publishes completion via g_load_done.
void load_task(void*)
{
    for (;;) {
        if (g_load_request.exchange(false)) {
            std::optional<LoadedBook> book = g_use_first ? load_first_book() : load_book(g_pending_path);
            if (!book) book = load_book("");          // sample fallback (never null)
            if (book) {
                g_index        = std::move(book->index);
                g_loaded_title = book->title;
            }
            g_load_done.store(true);
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

} // namespace

extern "C" void rsvp_loading_screen_create(void)
{
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    g_loading_scr = lv_obj_create(scr);
    lv_obj_remove_style_all(g_loading_scr);
    lv_obj_set_size(g_loading_scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_loading_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_loading_scr, LV_OPA_COVER, 0);

    lv_obj_t* lbl = lv_label_create(g_loading_scr);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xf2f5fa), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
    lv_label_set_text(lbl, "Loading...");
    lv_obj_center(lbl);

    // The inflate path uses a ~32KB tinfl_decompressor on the stack, so the load task
    // needs a big stack (48KB fits the largest free internal block at this point).
    if (xTaskCreatePinnedToCore(load_task, "bookload", 48 * 1024, nullptr, 3, nullptr, 1) != pdPASS)
        ESP_LOGE("ui", "failed to create book-load task");
    // app_main polls rsvp_book_ready(), then calls rsvp_build_reader_screen() to build the
    // reader on the main task under the LVGL lock (where the loading screen was set up).
}

extern "C" bool rsvp_book_ready(void)
{
    return g_load_done.load();
}

extern "C" void rsvp_build_reader_screen(void)
{
    if (g_loading_scr) { lv_obj_del(g_loading_scr); g_loading_scr = nullptr; }
    build_reader(g_loaded_title);
}

namespace {
// Polls the background load; once done, builds the reader and self-deletes.
void open_done_timer_cb(lv_timer_t* t)
{
    if (!g_load_done.load()) return;
    build_reader(g_loaded_title);
    lv_timer_del(t);
}
} // namespace

extern "C" void rsvp_open_book_path(const char* path)
{
    rsvp_reader_save_position();          // save the outgoing book first
    if (g_tick_timer) { lv_timer_del(g_tick_timer); g_tick_timer = nullptr; }
    if (g_player)     { delete g_player; g_player = nullptr; }   // stop before g_index is replaced

    g_pending_path = path ? path : "";
    g_use_first = false;
    g_load_done.store(false);

    g_loading_scr = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(g_loading_scr);
    lv_obj_set_size(g_loading_scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_loading_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_loading_scr, LV_OPA_COVER, 0);
    lv_obj_t* lbl = lv_label_create(g_loading_scr);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xf2f5fa), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
    lv_label_set_text(lbl, "Loading...");
    lv_obj_center(lbl);

    g_load_request.store(true);    // hand off to the persistent load task (created at boot)
    lv_timer_create(open_done_timer_cb, 50, nullptr);
}

// Create the persistent book-load task once at boot. Its 48KB stack lives in PSRAM: the
// inflate path needs a big stack, and internal RAM no longer has a contiguous 48KB block
// once the WiFi stack is linked. The task does only SD + CPU work (never flash writes), so
// a PSRAM stack is safe; this also frees 48KB internal for SD-write DMA during uploads.
extern "C" void rsvp_reader_init(void)
{
    if (xTaskCreatePinnedToCoreWithCaps(load_task, "bookload", 48 * 1024, nullptr, 3, nullptr, 1,
                                        MALLOC_CAP_SPIRAM) != pdPASS)
        ESP_LOGE("ui", "failed to create book-load task");
    lv_timer_create(clock_timer_cb, 20000, nullptr);   // refresh the clock every 20s
    // PWR long-press -> power off, from any screen. (Used to be registered by the old
    // loading-screen boot path, which the menu-first boot no longer calls.)
    power_bsp_set_shutdown_cb(on_shutdown_requested);
    lv_timer_create(shutdown_timer_cb, 100, nullptr);
}

extern "C" void rsvp_reader_save_position(void)
{
    if (g_player && g_index.wordCount() > 0 && !g_book_path.empty())
        save_position(g_book_path, static_cast<std::uint32_t>(g_player->index()));
}

extern "C" void rsvp_reader_pause(void)
{
    if (g_player && g_player->isPlaying()) { g_player->togglePlay(); update_status(); }
}

extern "C" void rsvp_reader_apply_settings(void)
{
    if (g_player == nullptr) return;
    g_wpm = settings().wpm;
    PacingConfig cfg = g_player->config();
    cfg.wpm = g_wpm;
    g_player->setConfig(cfg);
    if (g_prev && g_next) {
        if (settings().show_flankers) {
            lv_obj_clear_flag(g_prev, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_next, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_prev, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(g_next, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (g_pre && g_orp && g_post) {                  // apply font size live
        const lv_font_t* f = font_for(settings().font);
        lv_obj_set_style_text_font(g_pre,  f, 0);
        lv_obj_set_style_text_font(g_orp,  f, 0);
        lv_obj_set_style_text_font(g_post, f, 0);
        refresh_word();
    }
    update_status();
}

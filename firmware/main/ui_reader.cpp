#include "ui_reader.h"

#include "lvgl.h"

#include "rsvp/tokenize.hpp"
#include "rsvp/player.hpp"
#include "rsvp/orp.hpp"
#include "rsvp/pacing.hpp"

#include <cstdio>

using namespace rsvp;

namespace {

Document    g_doc;
Player*     g_player  = nullptr;
int         g_wpm     = 300;
std::size_t g_lastIdx = static_cast<std::size_t>(-1);

lv_obj_t *g_scr      = nullptr;
lv_obj_t *g_pre      = nullptr;
lv_obj_t *g_orp      = nullptr;
lv_obj_t *g_post     = nullptr;
lv_obj_t *g_prev     = nullptr;
lv_obj_t *g_next     = nullptr;
lv_obj_t *g_wpm_lbl  = nullptr;
lv_obj_t *g_tick_top = nullptr;
lv_obj_t *g_tick_bot = nullptr;

// Re-render the current word: split at the ORP, pin the red letter to screen centre,
// flank pre/post + prev/next words, and reposition the focal ticks over the ORP letter.
void refresh_word()
{
    if (g_player == nullptr || g_doc.empty()) return;
    const std::size_t idx = g_player->index();

    const OrpSplit s = orpSplit(g_player->current().text);
    lv_label_set_text(g_pre,  s.pre.c_str());
    lv_label_set_text(g_orp,  s.orp.c_str());
    lv_label_set_text(g_post, s.post.c_str());

    lv_label_set_text(g_prev, idx > 0 ? g_doc.tokens[idx - 1].text.c_str() : "");
    lv_label_set_text(g_next, (idx + 1 < g_doc.size()) ? g_doc.tokens[idx + 1].text.c_str() : "");

    lv_obj_align(g_orp, LV_ALIGN_CENTER, 0, 0);
    lv_obj_update_layout(g_scr);
    lv_obj_align_to(g_pre,      g_orp, LV_ALIGN_OUT_LEFT_MID,   0, 0);
    lv_obj_align_to(g_post,     g_orp, LV_ALIGN_OUT_RIGHT_MID,  0, 0);
    lv_obj_align_to(g_tick_top, g_orp, LV_ALIGN_OUT_TOP_MID,    0, -10);
    lv_obj_align_to(g_tick_bot, g_orp, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d wpm . %d%%", g_wpm,
                  static_cast<int>(g_player->progress() * 100.0 + 0.5));
    lv_label_set_text(g_wpm_lbl, buf);
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

extern "C" void rsvp_reading_screen_create(void)
{
    g_scr = lv_screen_active();
    lv_obj_set_style_bg_color(g_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);

    const lv_color_t dim   = lv_color_hex(0x8893a6);
    const lv_color_t faint = lv_color_hex(0x39414f);
    const lv_color_t white = lv_color_hex(0xf2f5fa);
    const lv_color_t red   = lv_color_hex(0xff3b3b);

    // top status
    lv_obj_t *batt = make_label(g_scr, dim, &lv_font_montserrat_16);
    lv_label_set_text(batt, "84%");
    lv_obj_align(batt, LV_ALIGN_TOP_LEFT, 10, 6);

    lv_obj_t *clk = make_label(g_scr, dim, &lv_font_montserrat_16);
    lv_label_set_text(clk, "2:14");
    lv_obj_align(clk, LV_ALIGN_TOP_RIGHT, -10, 6);

    // faint flankers (previous / next word)
    g_prev = make_label(g_scr, faint, &lv_font_montserrat_16);
    lv_obj_align(g_prev, LV_ALIGN_LEFT_MID, 18, 0);
    g_next = make_label(g_scr, faint, &lv_font_montserrat_16);
    lv_obj_align(g_next, LV_ALIGN_RIGHT_MID, -18, 0);

    // the word: pre + ORP(red) + post, ORP pinned to screen centre
    g_pre  = make_label(g_scr, white, &lv_font_montserrat_48);
    g_orp  = make_label(g_scr, red,   &lv_font_montserrat_48);
    g_post = make_label(g_scr, white, &lv_font_montserrat_48);

    // focal ticks
    static lv_style_t tick;
    lv_style_init(&tick);
    lv_style_set_bg_color(&tick, lv_color_hex(0xcdd6e6));
    lv_style_set_bg_opa(&tick, LV_OPA_40);

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

    // --- engine: tokenize a built-in sample and drive the Player ---
    g_doc = tokenizePlainText(
        "Rapid serial visual presentation shows one word at a time. "
        "Your eyes stay still while the words flow past you. "
        "This little reader is now alive on the hardware!");
    PacingConfig cfg;
    cfg.wpm = g_wpm;
    static Player player(g_doc, cfg);
    g_player = &player;
    g_player->play();

    refresh_word();
    lv_timer_create(tick_cb, 33, nullptr);
}

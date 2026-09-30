#include "champions_feed.h"
#include "champions_assets.h"
#include "champions_model.h"
#include "passport_net.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>
#include <time.h>

#define BG 0x0B0B0D
#define CREAM 0xF2E8D5
#define GOLD 0xD7B56D
#define MUTED 0xA89E8A
#define RED 0xC93A3A

static const char *TAG = "champions";
static champions_snapshot_t s_data, s_fetch, s_render;
static SemaphoreHandle_t s_data_mutex;
static QueueHandle_t s_keys;
static volatile bool s_ready, s_error, s_refresh;
static volatile int64_t s_updated;
static lv_obj_t *s_screen, *s_battery, *s_title, *s_match, *s_number, *s_detail, *s_info, *s_footer;
static lv_obj_t *s_brand, *s_stage_mark, *s_tile_a, *s_tile_b;
static lv_obj_t *s_logo_a, *s_logo_b, *s_team_a, *s_team_b;
static lv_obj_t *s_win_dot[2][3];
static lv_obj_t *s_today_logo[2][2], *s_today_match[2], *s_today_time[2];
static champions_pages_t s_pages;
typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } app_key_event_t;

static int64_t uptime(void) { return esp_timer_get_time() / 1000000; }
static bool valid_clock(int64_t now) { return now > 1700000000; }

static lv_obj_t *label(int y, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(s_screen);
    lv_obj_set_pos(obj, 16, y);
    lv_obj_set_width(obj, 208);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    return obj;
}

static lv_obj_t *logo_tile(int x)
{
    lv_obj_t *obj = lv_obj_create(s_screen);
    lv_obj_set_pos(obj, x, 95);
    lv_obj_set_size(obj, 68, 68);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x17171D), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(GOLD), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 6, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static void hide_match_art(void)
{
    lv_obj_t *parts[] = {s_tile_a, s_tile_b, s_logo_a, s_logo_b, s_team_a, s_team_b,
                         s_stage_mark, s_today_match[0], s_today_match[1],
                         s_today_time[0], s_today_time[1]};
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i)
        lv_obj_add_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
    for (int row = 0; row < 2; ++row)
        for (int side = 0; side < 2; ++side)
            lv_obj_add_flag(s_today_logo[row][side], LV_OBJ_FLAG_HIDDEN);
    for (int side = 0; side < 2; ++side)
        for (int dot = 0; dot < 3; ++dot)
            lv_obj_add_flag(s_win_dot[side][dot], LV_OBJ_FLAG_HIDDEN);
}

static void show_series_dots(const champions_match_t *match)
{
    int total = match->best_of == 5 ? 3 : match->best_of == 3 ? 2 : 0;
    if (!total && match->status == CHAMPIONS_FINISHED)
        total = match->score_a >= 3 || match->score_b >= 3 ? 3 : 2;
    for (int side = 0; side < 2; ++side) {
        int wins = side == 0 ? match->score_a : match->score_b;
        for (int dot = 0; dot < total; ++dot) {
            lv_obj_t *obj = s_win_dot[side][dot];
            int center = side == 0 ? 56 : 184;
            lv_obj_set_pos(obj, center - (total * 13 - 5) / 2 + dot * 13, 190);
            lv_obj_set_style_bg_opa(obj, dot < wins ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void show_logo(lv_obj_t *obj, int team_index, bool small)
{
    const lv_image_dsc_t *src = champions_assets_logo(team_index, small);
    if (src) {
        if (small) {
            lv_obj_set_style_bg_color(obj, lv_color_hex(champions_assets_light_background(team_index) ?
                                     0xC9B997 : 0x17171D), 0);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(obj, 3, 0);
        }
        lv_image_set_src(obj, src);
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void screen_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    s_brand = lv_image_create(s_screen);
    if (champions_assets_mark(true)) lv_image_set_src(s_brand, champions_assets_mark(true));
    else lv_obj_add_flag(s_brand, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_brand, 17, 17);
    lv_obj_t *head = label(18, &lv_font_montserrat_20, CREAM);
    lv_obj_set_pos(head, 52, 18);
    lv_obj_set_width(head, 144);
    lv_obj_set_style_text_align(head, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(head, "CHAMPIONS");
    lv_obj_t *city = label(43, &lv_font_montserrat_14, RED);
    lv_obj_set_pos(city, 54, 43);
    lv_obj_set_width(city, 165);
    lv_obj_set_style_text_align(city, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(city, "SHANGHAI 2026");
    s_battery = label(19, &lv_font_montserrat_14, MUTED);
    lv_obj_set_pos(s_battery, 187, 19);
    lv_obj_set_width(s_battery, 35);
    lv_obj_t *rule = lv_obj_create(s_screen);
    lv_obj_set_pos(rule, 17, 64);
    lv_obj_set_size(rule, 206, 2);
    lv_obj_set_style_bg_color(rule, lv_color_hex(GOLD), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_radius(rule, 0, 0);
    lv_obj_set_style_pad_all(rule, 0, 0);
    lv_obj_t *red_rule = lv_obj_create(s_screen);
    lv_obj_set_pos(red_rule, 17, 64);
    lv_obj_set_size(red_rule, 38, 2);
    lv_obj_set_style_bg_color(red_rule, lv_color_hex(RED), 0);
    lv_obj_set_style_bg_opa(red_rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(red_rule, 0, 0);
    lv_obj_set_style_radius(red_rule, 0, 0);
    lv_obj_set_style_pad_all(red_rule, 0, 0);
    s_title = label(73, &lv_font_montserrat_14, MUTED);
    s_tile_a = logo_tile(22);
    s_tile_b = logo_tile(150);
    s_logo_a = lv_image_create(s_screen);
    s_logo_b = lv_image_create(s_screen);
    lv_obj_set_pos(s_logo_a, 30, 103);
    lv_obj_set_pos(s_logo_b, 158, 103);
    s_match = label(118, &lv_font_montserrat_20, RED);
    s_team_a = label(167, &lv_font_montserrat_20, CREAM);
    lv_obj_set_pos(s_team_a, 16, 167);
    lv_obj_set_width(s_team_a, 80);
    s_team_b = label(167, &lv_font_montserrat_20, CREAM);
    lv_obj_set_pos(s_team_b, 144, 167);
    lv_obj_set_width(s_team_b, 80);
    for (int side = 0; side < 2; ++side)
        for (int dot = 0; dot < 3; ++dot) {
            lv_obj_t *obj = lv_obj_create(s_screen);
            lv_obj_set_size(obj, 8, 8);
            lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(obj, 1, 0);
            lv_obj_set_style_border_color(obj, lv_color_hex(GOLD), 0);
            lv_obj_set_style_bg_color(obj, lv_color_hex(GOLD), 0);
            lv_obj_set_style_pad_all(obj, 0, 0);
            s_win_dot[side][dot] = obj;
        }
    s_number = label(192, &lv_font_montserrat_48, GOLD);
    s_detail = label(250, &lv_font_montserrat_20, CREAM);
    s_info = label(278, &lv_font_montserrat_14, MUTED);
    s_footer = label(300, &lv_font_montserrat_14, MUTED);
    s_stage_mark = lv_image_create(s_screen);
    if (champions_assets_mark(false)) lv_image_set_src(s_stage_mark, champions_assets_mark(false));
    lv_obj_set_pos(s_stage_mark, 85, 105);
    for (int row = 0; row < 2; ++row) {
        int y = row == 0 ? 148 : 214;
        s_today_logo[row][0] = lv_image_create(s_screen);
        s_today_logo[row][1] = lv_image_create(s_screen);
        lv_obj_set_pos(s_today_logo[row][0], 22, y);
        lv_obj_set_pos(s_today_logo[row][1], 196, y);
        s_today_match[row] = label(y, &lv_font_montserrat_14, CREAM);
        lv_obj_set_pos(s_today_match[row], 48, y);
        lv_obj_set_width(s_today_match[row], 144);
        s_today_time[row] = label(y + 25, &lv_font_montserrat_14, GOLD);
    }
    hide_match_art();
    lv_screen_load(s_screen);
}

static void local_time(int64_t utc, char out[16])
{
    time_t shifted = (time_t)(utc + 8 * 3600);
    struct tm parts;
    gmtime_r(&shifted, &parts);
    snprintf(out, 16, "%02d:%02d", parts.tm_hour, parts.tm_min);
}

static void render_home(const champions_snapshot_t *data, int64_t now)
{
    lv_obj_set_style_text_color(s_title, lv_color_hex(MUTED), 0);
    lv_obj_set_style_text_font(s_match, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_match, lv_color_hex(RED), 0);
    lv_obj_set_pos(s_match, 16, 120);
    lv_obj_set_style_text_font(s_number, &lv_font_montserrat_48, 0);
    lv_obj_set_pos(s_number, 16, 193);
    lv_obj_set_style_text_font(s_detail, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(s_detail, 16, 251);
    lv_obj_set_pos(s_info, 16, 279);
    lv_obj_set_style_text_color(s_number, lv_color_hex(GOLD), 0);
    int selected = champions_home_match(data, now);
    const champions_match_t *match = selected >= 0 ? &data->matches[selected] : NULL;
    if (data->has_recent_final && (!match || match->status != CHAMPIONS_LIVE))
        match = &data->recent_final;
    if (!match) {
        lv_label_set_text(s_title, s_error ? "DATA UNAVAILABLE" : "NO MATCH NOW");
        if (champions_assets_mark(false)) lv_obj_remove_flag(s_stage_mark, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_match, 16, 183);
        lv_obj_set_style_text_color(s_match, lv_color_hex(CREAM), 0);
        lv_label_set_text(s_match, "CHAMPIONS");
        lv_obj_set_pos(s_number, 16, 214);
        lv_obj_set_style_text_font(s_number, &lv_font_montserrat_28, 0);
        lv_label_set_text(s_number, "--:--");
        lv_obj_set_pos(s_detail, 16, 253);
        lv_label_set_text(s_detail, "WAIT FOR SCHEDULE");
        lv_label_set_text(s_info, "SHANGHAI 2026");
        return;
    }
    lv_obj_remove_flag(s_tile_a, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_tile_b, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_team_a, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_team_b, LV_OBJ_FLAG_HIDDEN);
    show_logo(s_logo_a, champions_team_index(match->team_a), false);
    show_logo(s_logo_b, champions_team_index(match->team_b), false);
    lv_obj_set_style_bg_color(s_tile_a,
        lv_color_hex(champions_assets_light_background(champions_team_index(match->team_a)) ?
                     0xC9B997 : 0x17171D), 0);
    lv_obj_set_style_bg_color(s_tile_b,
        lv_color_hex(champions_assets_light_background(champions_team_index(match->team_b)) ?
                     0xC9B997 : 0x17171D), 0);
    lv_label_set_text(s_team_a, match->team_a);
    lv_label_set_text(s_team_b, match->team_b);
    lv_label_set_text(s_match, "VS");
    char text[64];
    if (match->status == CHAMPIONS_LIVE || match->status == CHAMPIONS_FINISHED) {
        show_series_dots(match);
        lv_obj_set_style_text_color(s_title, lv_color_hex(match->status == CHAMPIONS_LIVE ? RED : MUTED), 0);
        lv_label_set_text(s_title, match->status == CHAMPIONS_LIVE ? "LIVE" : "FINAL");
        lv_obj_set_pos(s_number, 16, 206);
        if (match->status == CHAMPIONS_LIVE && match->has_round_score)
            snprintf(text, sizeof(text), "%d : %d", match->round_a, match->round_b);
        else if (match->status == CHAMPIONS_LIVE) snprintf(text, sizeof(text), "--:--");
        else snprintf(text, sizeof(text), "%d : %d", match->score_a, match->score_b);
        lv_label_set_text(s_number, text);
        lv_obj_set_pos(s_detail, 16, 258);
        if (match->status == CHAMPIONS_LIVE)
            lv_label_set_text(s_detail, match->has_round_score ? "ROUND SCORE" : "ROUNDS SYNCING");
        else {
            int next = champions_home_match(data, now);
            if (next >= 0 && data->matches[next].status == CHAMPIONS_UPCOMING) {
                char next_time[16];
                local_time(data->matches[next].start_utc, next_time);
                snprintf(text, sizeof(text), "NEXT %s", next_time);
                lv_label_set_text(s_detail, text);
            } else lv_label_set_text(s_detail, "MATCH COMPLETE");
        }
    } else {
        lv_obj_set_style_text_color(s_title, lv_color_hex(MUTED), 0);
        lv_label_set_text(s_title, "NEXT MATCH");
        local_time(match->start_utc, text);
        lv_label_set_text(s_number, text);
        int64_t seconds = match->start_utc - now;
        if (!valid_clock(now)) lv_label_set_text(s_detail, "SYNCING CLOCK");
        else if (seconds >= 3600) {
            snprintf(text, sizeof(text), "IN %lldH", (long long)((seconds + 3599) / 3600));
            lv_label_set_text(s_detail, text);
        } else if (seconds > 0) {
            snprintf(text, sizeof(text), "IN %lldM", (long long)((seconds + 59) / 60));
            lv_label_set_text(s_detail, text);
        } else lv_label_set_text(s_detail, "STARTING SOON");
    }
    lv_obj_set_pos(s_info, 16, 281);
    lv_label_set_text(s_info, match->stage[0] ? match->stage : "CHAMPIONS SHANGHAI");
}

static void render_schedule(const champions_snapshot_t *data, int64_t now)
{
    lv_obj_set_style_text_color(s_title, lv_color_hex(MUTED), 0);
    lv_obj_set_style_text_font(s_match, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_match, lv_color_hex(CREAM), 0);
    lv_obj_set_pos(s_match, 16, 102);
    lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_title, "SCHEDULE");
    int indices[2];
    int64_t day_utc;
    size_t count = champions_schedule_matches(data, now, indices, &day_utc);
    time_t shifted = (time_t)((count ? day_utc : now) + 8 * 3600);
    struct tm date;
    gmtime_r(&shifted, &date);
    char line[64];
    snprintf(line, sizeof(line), "%02d/%02d", date.tm_mon + 1, date.tm_mday);
    lv_label_set_text(s_match, count || valid_clock(now) ? line : "--/--");
    for (size_t row = 0; row < count; ++row) {
        const champions_match_t *m = &data->matches[indices[row]];
        char start[16];
        local_time(m->start_utc, start);
        show_logo(s_today_logo[row][0], champions_team_index(m->team_a), true);
        show_logo(s_today_logo[row][1], champions_team_index(m->team_b), true);
        lv_obj_remove_flag(s_today_match[row], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_today_time[row], LV_OBJ_FLAG_HIDDEN);
        snprintf(line, sizeof(line), "%s  VS  %s", m->team_a, m->team_b);
        lv_label_set_text(s_today_match[row], line);
        if (m->status == CHAMPIONS_FINISHED)
            snprintf(line, sizeof(line), "FINAL  %d : %d", m->score_a, m->score_b);
        else if (m->status == CHAMPIONS_LIVE)
            snprintf(line, sizeof(line), "LIVE  %d : %d", m->score_a, m->score_b);
        else snprintf(line, sizeof(line), "%s  CST", start);
        lv_obj_set_style_text_color(s_today_time[row],
            lv_color_hex(m->status == CHAMPIONS_LIVE ? RED : GOLD), 0);
        lv_label_set_text(s_today_time[row], line);
    }
    if (count) lv_obj_add_flag(s_number, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_obj_set_style_text_font(s_number, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(s_number, lv_color_hex(CREAM), 0);
        lv_obj_set_pos(s_number, 16, 183);
        lv_label_set_text(s_number, "NO UPCOMING MATCH");
    }
    lv_obj_set_pos(s_info, 16, 278);
    lv_label_set_text(s_info, "CHINA TIME  UTC+8");
}

static void render_stage(int64_t now)
{
    lv_obj_set_style_text_color(s_title, lv_color_hex(MUTED), 0);
    lv_obj_set_style_text_font(s_match, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_match, lv_color_hex(RED), 0);
    lv_obj_set_pos(s_match, 16, 178);
    lv_label_set_text(s_match, "FRAG OR DIE");
    if (champions_assets_mark(false)) lv_obj_remove_flag(s_stage_mark, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(s_number, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_number, lv_color_hex(GOLD), 0);
    lv_obj_set_pos(s_number, 16, 202);
    lv_obj_set_style_text_font(s_detail, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(s_detail, 16, 243);
    lv_obj_set_pos(s_info, 16, 277);
    lv_label_set_text(s_title, "TOURNAMENT");
    time_t shifted = (time_t)(now + 8 * 3600);
    struct tm date;
    gmtime_r(&shifted, &date);
    bool playoff = valid_clock(now) && (date.tm_mon > 9 || (date.tm_mon == 9 && date.tm_mday >= 7));
    lv_label_set_text(s_number, playoff ? "PLAYOFFS" : "GROUP STAGE");
    lv_label_set_text(s_detail, "SEP 24 - OCT 18");
    lv_label_set_text(s_info, "VALORANT CHAMPIONS 2026");
}

static void render_setup(void)
{
    lv_obj_set_style_text_color(s_title, lv_color_hex(GOLD), 0);
    lv_obj_set_style_text_font(s_match, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_match, lv_color_hex(CREAM), 0);
    lv_obj_set_pos(s_match, 16, 123);
    lv_obj_set_style_text_font(s_number, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_number, lv_color_hex(GOLD), 0);
    lv_obj_set_pos(s_number, 16, 162);
    lv_obj_set_style_text_font(s_detail, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(s_detail, 16, 222);
    lv_obj_set_pos(s_info, 16, 257);
    lv_label_set_text(s_title, "WIFI SETUP");
    lv_label_set_text(s_match, passport_net_ap_name());
    lv_label_set_text(s_number, passport_net_ap_password());
    lv_label_set_text(s_detail, "OPEN 192.168.4.1");
    lv_label_set_text(s_info, "JOIN THE WIFI ABOVE");
}

static void render(const champions_snapshot_t *data, int64_t now, int battery)
{
    char text[48];
    hide_match_art();
    lv_obj_remove_flag(s_match, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_number, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_info, LV_OBJ_FLAG_HIDDEN);
    if (battery >= 0) {
        snprintf(text, sizeof(text), "%d%%", battery);
        lv_label_set_text(s_battery, text);
    } else lv_label_set_text(s_battery, "");
    if (passport_net_state() == PASSPORT_NET_SETUP) render_setup();
    else if (s_pages.page == 1) render_schedule(data, now);
    else if (s_pages.page == 2) render_stage(now);
    else render_home(data, now);
    const char *dots[] = {"O . .", ". O .", ". . O"};
    snprintf(text, sizeof(text), "%s%s%s", s_pages.locked ? "LOCK" : dots[s_pages.page],
             !passport_net_online() ? "  OFFLINE" : (s_error ? "  DATA ERROR" : ""),
             s_updated && valid_clock(now) && now - s_updated > 1800 ? "  STALE" : "");
    lv_label_set_text(s_footer, text);
}

static void key_callback(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    app_key_event_t key = {.button = button, .event = event};
    if (s_keys) xQueueSend(s_keys, &key, 0);
}

static void publish_snapshot(void)
{
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    s_data = s_fetch;
    s_ready = true;
    s_updated = time(NULL);
    xSemaphoreGive(s_data_mutex);
}

static void network_task(void *arg)
{
    (void)arg;
    int64_t next_schedule = 0, next_round = 0;
    int schedule_retry = 0, round_retry = 0;
    bool schedule_error = false, round_error = false;
    for (;;) {
        passport_net_service();
        if (passport_net_online()) {
            if (s_refresh) {
                s_refresh = false;
                next_schedule = next_round = 0;
            }
            if (uptime() >= next_schedule) {
                int old_live = champions_live_match(&s_fetch);
                int old_id = old_live >= 0 ? s_fetch.matches[old_live].id : 0;
                int old_a = old_live >= 0 ? s_fetch.matches[old_live].score_a : 0;
                int old_b = old_live >= 0 ? s_fetch.matches[old_live].score_b : 0;
                esp_err_t err = champions_feed_refresh(&s_fetch);
                schedule_error = err != ESP_OK;
                if (err == ESP_OK) {
                    schedule_retry = 0;
                    publish_snapshot();
                    next_schedule = uptime() + champions_schedule_delay(&s_fetch, time(NULL));
                    int live = champions_live_match(&s_fetch);
                    if (live >= 0 && (s_fetch.matches[live].id != old_id ||
                        s_fetch.matches[live].score_a != old_a ||
                        s_fetch.matches[live].score_b != old_b)) next_round = 0;
                } else {
                    ESP_LOGW(TAG, "Feed refresh failed: %s", esp_err_to_name(err));
                    schedule_retry = champions_retry_delay(schedule_retry);
                    next_schedule = uptime() + schedule_retry;
                }
            }
            if (champions_live_match(&s_fetch) >= 0) {
                if (uptime() >= next_round) {
                    esp_err_t err = champions_feed_refresh_rounds(&s_fetch);
                    round_error = err != ESP_OK;
                    if (err == ESP_OK) {
                        round_retry = 0;
                        publish_snapshot();
                        next_round = uptime() + CHAMPIONS_ROUND_REFRESH_SECONDS;
                        if (champions_live_match(&s_fetch) < 0) next_schedule = 0;
                    } else {
                        round_retry = champions_retry_delay(round_retry);
                        next_round = uptime() + round_retry;
                    }
                }
            } else {
                round_error = false;
                round_retry = 0;
                next_round = 0;
            }
            s_error = schedule_error || round_error;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    if (!champions_assets_init()) ESP_LOGE(TAG, "Champions image pack is invalid");
    s_data_mutex = xSemaphoreCreateMutex();
    s_keys = xQueueCreate(8, sizeof(app_key_event_t));
    if (!s_data_mutex || !s_keys) return;
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    if (!bsp_lvgl_lock(1000)) return;
    screen_create();
    bsp_lvgl_unlock();
    bsp_display_backlight(85);
    bool has_battery = bsp_battery_init() == ESP_OK;
    if (bsp_button_init(key_callback, NULL) != ESP_OK) ESP_LOGW(TAG, "Buttons unavailable");
    if (passport_net_init() == ESP_OK) {
        if (xTaskCreate(network_task, "vct_network", 8192, NULL, 4, NULL) != pdPASS)
            ESP_LOGE(TAG, "Network task creation failed");
    } else ESP_LOGE(TAG, "Network initialization failed");
    s_pages.changed_at = uptime();
    int battery = -1;
    int64_t battery_at = 0;
    for (;;) {
        app_key_event_t key;
        while (xQueueReceive(s_keys, &key, 0) == pdTRUE) {
            if (key.event == BSP_BTN_LONG && key.button == BSP_BTN_OK) passport_net_request_setup();
            else if (key.event == BSP_BTN_CLICK && key.button == BSP_BTN_OK)
                champions_pages_toggle_lock(&s_pages, uptime());
            else if (key.event == BSP_BTN_DOUBLE && key.button == BSP_BTN_OK) s_refresh = true;
            else if (key.event == BSP_BTN_CLICK && (key.button == BSP_BTN_UP || key.button == BSP_BTN_DOWN)) {
                champions_pages_select(&s_pages, key.button == BSP_BTN_UP ? -1 : 1, uptime());
            }
        }
        int64_t now = uptime();
        champions_pages_tick(&s_pages, now);
        if (has_battery && now - battery_at >= 60) {
            battery = bsp_battery_soc();
            battery_at = now;
        }
        xSemaphoreTake(s_data_mutex, portMAX_DELAY);
        s_render = s_data;
        xSemaphoreGive(s_data_mutex);
        if (bsp_lvgl_lock(1000)) {
            render(&s_render, time(NULL), battery);
            bsp_lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHAMPIONS_MAX_MATCHES 40
#define CHAMPIONS_TEAM_COUNT 16
#define CHAMPIONS_ROUND_REFRESH_SECONDS 5

typedef enum {
    CHAMPIONS_UPCOMING = 0,
    CHAMPIONS_LIVE,
    CHAMPIONS_FINISHED,
} champions_status_t;

typedef struct {
    int id;
    int64_t start_utc;
    char team_a[16];
    char team_b[16];
    char stage[24];
    champions_status_t status;
    int score_a;
    int score_b;
    int best_of;
    int round_a;
    int round_b;
    bool has_round_score;
} champions_match_t;

typedef struct {
    champions_match_t matches[CHAMPIONS_MAX_MATCHES];
    size_t count;
    champions_match_t recent_final;
    bool has_recent_final;
} champions_snapshot_t;

typedef struct {
    int page;
    bool locked;
    int64_t changed_at;
    int64_t manual_until;
} champions_pages_t;

bool champions_parse_ics(const char *data, size_t length, champions_snapshot_t *out);
bool champions_apply_match_html(const char *html, size_t length, champions_snapshot_t *snapshot);
void champions_consume_html(char *buffer, size_t *used, champions_snapshot_t *snapshot);
bool champions_consume_live_detail(char *buffer, size_t *used, champions_match_t *match);
int champions_home_match(const champions_snapshot_t *snapshot, int64_t now_utc);
int champions_live_match(const champions_snapshot_t *snapshot);
int champions_schedule_delay(const champions_snapshot_t *snapshot, int64_t now_utc);
void champions_keep_rounds(champions_snapshot_t *updated, const champions_snapshot_t *previous);
int champions_retry_delay(int previous_seconds);
int champions_team_index(const char *code);
bool champions_parse_match_start(const char *html, size_t length, champions_match_t *match);
void champions_keep_schedule(champions_snapshot_t *updated, const champions_snapshot_t *previous, int64_t now_utc);
size_t champions_schedule_matches(const champions_snapshot_t *snapshot, int64_t now_utc,
                                 int indices[2], int64_t *day_utc);
void champions_pages_select(champions_pages_t *pages, int direction, int64_t now);
void champions_pages_toggle_lock(champions_pages_t *pages, int64_t now);
void champions_pages_tick(champions_pages_t *pages, int64_t now);


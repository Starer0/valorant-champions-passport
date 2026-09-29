#include "champions_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_calendar_uses_utc_and_folds_lines(void)
{
    const char *ics =
        "BEGIN:VCALENDAR\r\r\n"
        "BEGIN:VEVENT\r\r\n"
        "DTSTART:20260927T090000Z\r\r\n"
        "SUMMARY:100 Thieves vs. T1 - Valorant Champions 2026\r\r\n"
        "URL:https://www.vlr.gg/753444/100-thieves-vs-t1-valorant-champions-\r\r\n"
        " 2026-opening-a\r\r\n"
        "END:VEVENT\r\r\n"
        "BEGIN:VEVENT\r\r\n"
        "DTSTART:20260927T120000Z\r\r\n"
        "SUMMARY:JD Gaming vs. FUT Esports - Valorant Champions 2026\r\r\n"
        "URL:https://www.vlr.gg/753445/jdg-vs-fut\r\r\n"
        "END:VEVENT\r\r\n"
        "END:VCALENDAR\r\r\n";
    champions_snapshot_t snapshot = {0};
    assert(champions_parse_ics(ics, strlen(ics), &snapshot));
    assert(snapshot.count == 2);
    assert(strcmp(snapshot.matches[0].team_a, "100T") == 0);
    assert(strcmp(snapshot.matches[0].team_b, "T1") == 0);
    assert(snapshot.matches[0].id == 753444);
    assert(snapshot.matches[0].start_utc == 1790499600);
    assert(strcmp(snapshot.matches[1].team_a, "JDG") == 0);
    assert(strcmp(snapshot.matches[1].team_b, "FUT") == 0);
}

static void test_live_score_extracts_only_selected_match(void)
{
    const char *html =
        "<a href=\"/753445/jdg-vs-fut\" class=\"wf-module-item match-item mod-first\">"
        "<div class=\"ml mod-live\"><div class=\"ml-status\">LIVE</div></div>"
        "<div class=\"match-item-vs-team-score sp-mask\">1</div>"
        "<div class=\"match-item-vs-team-score sp-mask\">0</div>"
        "<div class=\"match-item-event-series text-of\">Opening (A)</div></a>";
    champions_snapshot_t snapshot = {0};
    snapshot.count = 1;
    snapshot.matches[0].id = 753444;
    assert(!champions_apply_match_html(html, strlen(html), &snapshot));
    assert(snapshot.matches[0].status == CHAMPIONS_UPCOMING);
    snapshot.matches[0].id = 753445;
    assert(champions_apply_match_html(html, strlen(html), &snapshot));
    assert(snapshot.matches[0].status == CHAMPIONS_LIVE);
    assert(snapshot.matches[0].score_a == 1);
    assert(snapshot.matches[0].score_b == 0);
    assert(strcmp(snapshot.matches[0].stage, "Opening (A)") == 0);
}

static void test_home_prioritizes_live_then_next(void)
{
    champions_snapshot_t snapshot = {0};
    snapshot.count = 2;
    snapshot.matches[0].start_utc = 100;
    snapshot.matches[0].status = CHAMPIONS_UPCOMING;
    snapshot.matches[1].start_utc = 200;
    snapshot.matches[1].status = CHAMPIONS_LIVE;
    assert(champions_home_match(&snapshot, 50) == 1);
    snapshot.matches[1].status = CHAMPIONS_UPCOMING;
    assert(champions_home_match(&snapshot, 50) == 0);
    assert(champions_home_match(&snapshot, 150) == 1);
}

static void test_recent_final_survives_calendar_rollover(void)
{
    const char *html =
        "<a class=\"wf-module-item match-item\" href=\"/753444/old-match\">"
        "<div class=\"match-item-vs-team-name\"><div class=\"text-of\">100 Thieves</div></div>"
        "<div class=\"match-item-vs-team-score sp-mask\">2</div>"
        "<div class=\"match-item-vs-team-name\"><div class=\"text-of\">T1</div></div>"
        "<div class=\"match-item-vs-team-score sp-mask\">1</div>"
        "<div class=\"ml mod-completed\"><div class=\"ml-eta mod-completed\">4m</div></div>"
        "</a>";
    champions_snapshot_t snapshot = {0};
    assert(champions_apply_match_html(html, strlen(html), &snapshot));
    assert(snapshot.has_recent_final);
    assert(strcmp(snapshot.recent_final.team_a, "100T") == 0);
    assert(strcmp(snapshot.recent_final.team_b, "T1") == 0);
    assert(snapshot.recent_final.score_a == 2);
    assert(snapshot.recent_final.score_b == 1);
}

static void test_html_stream_accepts_href_before_class_and_split_anchor(void)
{
    const char *chunks[] = {
        "<a href=\"/other\" class=\"nav\">other</a><a href=\"/753445/jdg-vs-fut\" cla",
        "ss=\"wf-module-item match-item mod-first\"><div class=\"ml mod-live\"></div>",
        "<div class=\"match-item-vs-team-score\">1</div>"
        "<div class=\"match-item-vs-team-score\">0</div></a>",
    };
    champions_snapshot_t snapshot = {0};
    snapshot.count = 1;
    snapshot.matches[0].id = 753445;
    char buffer[512] = {0};
    size_t used = 0;
    for (size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); ++i) {
        size_t n = strlen(chunks[i]);
        assert(used + n < sizeof(buffer));
        memcpy(buffer + used, chunks[i], n);
        used += n;
        champions_consume_html(buffer, &used, &snapshot);
    }
    assert(snapshot.matches[0].status == CHAMPIONS_LIVE);
    assert(snapshot.matches[0].score_a == 1);
    assert(snapshot.matches[0].score_b == 0);
}

static void test_valid_empty_calendar_is_not_a_network_error(void)
{
    const char *ics = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nEND:VCALENDAR\r\n";
    champions_snapshot_t snapshot = {0};
    assert(champions_parse_ics(ics, strlen(ics), &snapshot));
    assert(snapshot.count == 0);
}

static void test_old_final_does_not_hide_no_match_state(void)
{
    champions_snapshot_t snapshot = {0};
    snapshot.count = 1;
    snapshot.matches[0].start_utc = 100;
    snapshot.matches[0].status = CHAMPIONS_FINISHED;
    assert(champions_home_match(&snapshot, 100 + 2 * 3600) == 0);
    assert(champions_home_match(&snapshot, 100 + 4 * 3600) == -1);
}

static void test_team_logo_lookup_covers_champions_roster(void)
{
    const char *codes[] = {
        "100T", "EDG", "FUT", "G2", "GE", "JDG", "KC", "LOUD",
        "NRG", "NS", "PRX", "T1", "TL", "TYL", "VIT", "XLG",
    };
    for (int i = 0; i < CHAMPIONS_TEAM_COUNT; ++i)
        assert(champions_team_index(codes[i]) == i);
    assert(champions_team_index("TBD") == -1);
    assert(champions_team_index(NULL) == -1);
}

static void test_live_detail_stream_reads_rounds_and_best_of(void)
{
    const char *chunks[] = {
        "<div class=\"match-header-vs-note\">Bo5</div><div class=\"vm-stats-game mod-act",
        "ive\" data-game-id=\"12\"><div class=\"vm-stats-game-header\"><div class=\"team\">",
        "<div class=\"score \" style=\"x\"> 11 </div><div class=\"map\">Summit</div>",
        "<div class=\"team mod-right\"><div class=\"score \" style=\"x\"> 9 </div></div>",
    };
    champions_match_t match = {0};
    char buffer[512] = {0};
    size_t used = 0;
    for (size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); ++i) {
        size_t n = strlen(chunks[i]);
        assert(used + n < sizeof(buffer));
        memcpy(buffer + used, chunks[i], n);
        used += n;
        bool complete = champions_consume_live_detail(buffer, &used, &match);
        assert(complete == (i == 3));
    }
    assert(match.best_of == 5);
    assert(match.has_round_score);
    assert(match.round_a == 11 && match.round_b == 9);
}

static void test_live_detail_rejects_empty_scores(void)
{
    char html[] = "Bo3<div class=\"vm-stats-game mod-active\"><div class=\"vm-stats-game-header\">"
                  "<div class=\"score \">-</div><div class=\"score \">-</div>";
    champions_match_t match = {0};
    size_t used = strlen(html);
    assert(!champions_consume_live_detail(html, &used, &match));
    assert(match.best_of == 3 && !match.has_round_score);
}

static void test_refresh_retry_backs_off_and_resets(void)
{
    int delay = 0;
    assert((delay = champions_retry_delay(delay)) == 15);
    assert((delay = champions_retry_delay(delay)) == 30);
    assert((delay = champions_retry_delay(delay)) == 60);
    assert((delay = champions_retry_delay(delay)) == 120);
    assert(champions_retry_delay(delay) == 120);
    assert(champions_retry_delay(0) == 15);
}

static void test_separate_live_refresh_intervals(void)
{
    champions_snapshot_t snapshot = {0};
    assert(CHAMPIONS_ROUND_REFRESH_SECONDS == 5);
    assert(champions_schedule_delay(&snapshot, 1000) == 600);
    snapshot.count = 1;
    snapshot.matches[0] = (champions_match_t){.id = 7, .start_utc = 1200};
    assert(champions_schedule_delay(&snapshot, 1000) == 120);
    assert(champions_live_match(&snapshot) == -1);
    snapshot.matches[0].status = CHAMPIONS_LIVE;
    assert(champions_schedule_delay(&snapshot, 1000) == 60);
    assert(champions_live_match(&snapshot) == 0);
    snapshot.matches[0].status = CHAMPIONS_FINISHED;
    assert(champions_live_match(&snapshot) == -1);
}

static void test_schedule_refresh_keeps_rounds_only_for_same_series_score(void)
{
    champions_snapshot_t previous = {0}, updated = {0};
    previous.count = updated.count = 1;
    previous.matches[0] = (champions_match_t){.id = 7, .status = CHAMPIONS_LIVE,
        .score_a = 1, .round_a = 8, .round_b = 6, .has_round_score = true, .best_of = 3};
    updated.matches[0] = (champions_match_t){.id = 7, .status = CHAMPIONS_LIVE, .score_a = 1};
    champions_keep_rounds(&updated, &previous);
    assert(updated.matches[0].has_round_score && updated.matches[0].round_a == 8);
    assert(updated.matches[0].round_b == 6 && updated.matches[0].best_of == 3);
    updated.matches[0].has_round_score = false;
    updated.matches[0].score_a = 2;
    champions_keep_rounds(&updated, &previous);
    assert(!updated.matches[0].has_round_score);
    updated.matches[0].score_a = 1;
    updated.matches[0].status = CHAMPIONS_FINISHED;
    champions_keep_rounds(&updated, &previous);
    assert(!updated.matches[0].has_round_score);
}

int main(void)
{
    test_calendar_uses_utc_and_folds_lines();
    test_live_score_extracts_only_selected_match();
    test_home_prioritizes_live_then_next();
    test_recent_final_survives_calendar_rollover();
    test_html_stream_accepts_href_before_class_and_split_anchor();
    test_valid_empty_calendar_is_not_a_network_error();
    test_old_final_does_not_hide_no_match_state();
    test_team_logo_lookup_covers_champions_roster();
    test_live_detail_stream_reads_rounds_and_best_of();
    test_live_detail_rejects_empty_scores();
    test_refresh_retry_backs_off_and_resets();
    test_separate_live_refresh_intervals();
    test_schedule_refresh_keeps_rounds_only_for_same_series_score();
    puts("champions model: PASS");
    return 0;
}

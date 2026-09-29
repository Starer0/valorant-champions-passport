#include "champions_model.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned mp = month > 2 ? month - 3 : month + 9;
    const unsigned doy = (153 * mp + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static bool parse_utc(const char *s, int64_t *out)
{
    int y, mo, d, h, mi, sec;
    if (strlen(s) < 16 || sscanf(s, "%4d%2d%2dT%2d%2d%2dZ", &y, &mo, &d, &h, &mi, &sec) != 6) return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 59) return false;
    *out = days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + sec;
    return true;
}

static void short_team(const char *full, char out[16])
{
    static const struct { const char *full; const char *short_name; } names[] = {
        {"100 Thieves", "100T"}, {"JD Gaming", "JDG"}, {"JD GAMING", "JDG"},
        {"FUT Esports", "FUT"}, {"Paper Rex", "PRX"}, {"Team Heretics", "TH"},
        {"G2 Esports", "G2"}, {"Team Liquid", "TL"}, {"Nongshim RedForce", "NS"},
        {"Karmine Corp", "KC"}, {"Xi Lai Gaming", "XLG"}, {"Team Vitality", "VIT"},
        {"Global Esports", "GE"}, {"Leviatán", "LEV"}, {"TBD", "TBD"},
        {"EDward Gaming", "EDG"}, {"TYLOO", "TYL"},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(full, names[i].full) == 0) {
            snprintf(out, 16, "%s", names[i].short_name);
            return;
        }
    }
    size_t n = 0;
    while (full[n] && n < 8 && (unsigned char)full[n] < 128) {
        out[n] = (char)toupper((unsigned char)full[n]);
        ++n;
    }
    out[n] = '\0';
    if (!n) snprintf(out, 16, "TBD");
}

int champions_team_index(const char *code)
{
    static const char *const codes[CHAMPIONS_TEAM_COUNT] = {
        "100T", "EDG", "FUT", "G2", "GE", "JDG", "KC", "LOUD",
        "NRG", "NS", "PRX", "T1", "TL", "TYL", "VIT", "XLG",
    };
    if (!code) return -1;
    for (int i = 0; i < CHAMPIONS_TEAM_COUNT; ++i) {
        if (strcmp(code, codes[i]) == 0) return i;
    }
    return -1;
}

static void parse_summary(const char *value, champions_match_t *match)
{
    const char *vs = strstr(value, " vs. ");
    if (!vs) return;
    char team[64];
    size_t n = (size_t)(vs - value);
    if (n >= sizeof(team)) return;
    memcpy(team, value, n);
    team[n] = '\0';
    short_team(team, match->team_a);
    const char *end = strstr(vs + 5, " - ");
    if (!end) end = vs + 5 + strlen(vs + 5);
    n = (size_t)(end - (vs + 5));
    if (n >= sizeof(team)) return;
    memcpy(team, vs + 5, n);
    team[n] = '\0';
    short_team(team, match->team_b);
}

static void process_line(const char *line, champions_match_t *current,
                         champions_snapshot_t *out, bool *in_event)
{
    if (strcmp(line, "BEGIN:VEVENT") == 0) {
        *current = (champions_match_t){0};
        *in_event = true;
    } else if (strcmp(line, "END:VEVENT") == 0) {
        if (*in_event && current->id > 0 && current->start_utc > 0 &&
            current->team_a[0] && current->team_b[0] && out->count < CHAMPIONS_MAX_MATCHES) {
            out->matches[out->count++] = *current;
        }
        *in_event = false;
    } else if (*in_event && strncmp(line, "DTSTART:", 8) == 0) {
        parse_utc(line + 8, &current->start_utc);
    } else if (*in_event && strncmp(line, "SUMMARY:", 8) == 0) {
        parse_summary(line + 8, current);
    } else if (*in_event && strncmp(line, "URL:https://www.vlr.gg/", 23) == 0) {
        current->id = atoi(line + 23);
    }
}

bool champions_parse_ics(const char *data, size_t length, champions_snapshot_t *out)
{
    if (!data || !out || length == 0 || length > 32768) return false;
    *out = (champions_snapshot_t){0};
    champions_match_t current = {0};
    bool in_event = false;
    bool saw_calendar_begin = false;
    bool saw_calendar_end = false;
    char line[320];
    size_t used = 0;
    for (size_t i = 0; i < length;) {
        size_t start = i;
        while (i < length && data[i] != '\n') ++i;
        size_t end = i;
        if (i < length) ++i;
        while (end > start && data[end - 1] == '\r') --end;
        bool continuation = start < end && (data[start] == ' ' || data[start] == '\t');
        if (!continuation && used) {
            line[used] = '\0';
            if (strcmp(line, "BEGIN:VCALENDAR") == 0) saw_calendar_begin = true;
            if (strcmp(line, "END:VCALENDAR") == 0) saw_calendar_end = true;
            process_line(line, &current, out, &in_event);
            used = 0;
        }
        if (continuation) ++start;
        size_t part = end - start;
        if (used + part >= sizeof(line)) return false;
        memcpy(line + used, data + start, part);
        used += part;
    }
    if (used) {
        line[used] = '\0';
        if (strcmp(line, "BEGIN:VCALENDAR") == 0) saw_calendar_begin = true;
        if (strcmp(line, "END:VCALENDAR") == 0) saw_calendar_end = true;
        process_line(line, &current, out, &in_event);
    }
    return saw_calendar_begin && saw_calendar_end;
}

bool champions_apply_match_html(const char *html, size_t length, champions_snapshot_t *snapshot)
{
    if (!html || !snapshot || length == 0) return false;
    const char *href = strstr(html, "href=\"/");
    if (!href) return false;
    int id = atoi(href + 7);
    champions_match_t *match = NULL;
    for (size_t i = 0; i < snapshot->count; ++i) {
        if (snapshot->matches[i].id == id) {
            match = &snapshot->matches[i];
            break;
        }
    }
    if (!match && strstr(html, "ml mod-completed")) {
        const char *eta = strstr(html, "ml-eta mod-completed");
        if (eta && (eta = strchr(eta, '>')) != NULL) {
            ++eta;
            while (*eta && isspace((unsigned char)*eta)) ++eta;
            int age = atoi(eta);
            while (isdigit((unsigned char)*eta)) ++eta;
            if (*eta == 'm' && age <= 10) {
                match = &snapshot->recent_final;
                *match = (champions_match_t){.id = id};
                snapshot->has_recent_final = true;
                const char *name = html;
                for (int team_index = 0; team_index < 2; ++team_index) {
                    name = strstr(name, "match-item-vs-team-name");
                    if (!name) break;
                    name = strstr(name, "class=\"text-of\"");
                    if (!name || (name = strchr(name, '>')) == NULL) break;
                    ++name;
                    char full[64] = {0};
                    size_t used = 0;
                    bool in_tag = false;
                    while (*name && strncmp(name, "</div>", 6) != 0 && used < sizeof(full) - 1) {
                        if (*name == '<') in_tag = true;
                        else if (*name == '>') in_tag = false;
                        else if (!in_tag) full[used++] = *name;
                        ++name;
                    }
                    while (used && isspace((unsigned char)full[used - 1])) full[--used] = '\0';
                    char *trimmed = full;
                    while (*trimmed && isspace((unsigned char)*trimmed)) ++trimmed;
                    short_team(trimmed, team_index == 0 ? match->team_a : match->team_b);
                }
            }
        }
    }
    if (match) {
        if (strstr(html, "ml mod-live")) match->status = CHAMPIONS_LIVE;
        else if (strstr(html, "ml mod-completed")) match->status = CHAMPIONS_FINISHED;
        else match->status = CHAMPIONS_UPCOMING;
        const char *score = html;
        int values[2] = {0};
        int found = 0;
        while (found < 2 && (score = strstr(score, "match-item-vs-team-score")) != NULL) {
            score = strchr(score, '>');
            if (!score) break;
            ++score;
            while (*score && isspace((unsigned char)*score)) ++score;
            if (isdigit((unsigned char)*score)) values[found++] = atoi(score);
        }
        if (found == 2) {
            match->score_a = values[0];
            match->score_b = values[1];
        }
        const char *stage = strstr(html, "match-item-event-series text-of");
        if (stage && (stage = strchr(stage, '>')) != NULL) {
            ++stage;
            while (*stage && isspace((unsigned char)*stage)) ++stage;
            const char *end = strchr(stage, '<');
            if (end) {
                while (end > stage && isspace((unsigned char)end[-1])) --end;
                size_t n = (size_t)(end - stage);
                if (n > 0 && n < sizeof(match->stage)) {
                    memcpy(match->stage, stage, n);
                    match->stage[n] = '\0';
                }
            }
        }
        return true;
    }
    return false;
}

void champions_consume_html(char *buffer, size_t *used, champions_snapshot_t *snapshot)
{
    buffer[*used] = '\0';
    for (;;) {
        char *start = strstr(buffer, "<a ");
        if (!start) {
            size_t keep = *used < 3 ? *used : 3;
            memmove(buffer, buffer + *used - keep, keep);
            *used = keep;
            buffer[keep] = '\0';
            return;
        }
        char *tag_end = strchr(start, '>');
        if (!tag_end) {
            size_t remaining = *used - (size_t)(start - buffer);
            memmove(buffer, start, remaining);
            *used = remaining;
            buffer[remaining] = '\0';
            return;
        }
        char *match_class = strstr(start, "wf-module-item match-item");
        if (!match_class || match_class > tag_end) {
            char *next = tag_end + 1;
            size_t remaining = *used - (size_t)(next - buffer);
            memmove(buffer, next, remaining);
            *used = remaining;
            buffer[remaining] = '\0';
            continue;
        }
        char *end = strstr(start, "</a>");
        if (!end) {
            size_t remaining = *used - (size_t)(start - buffer);
            memmove(buffer, start, remaining);
            *used = remaining;
            buffer[remaining] = '\0';
            return;
        }
        end += 4;
        char saved = *end;
        *end = '\0';
        champions_apply_match_html(start, (size_t)(end - start), snapshot);
        *end = saved;
        size_t remainder = *used - (size_t)(end - buffer);
        memmove(buffer, end, remainder);
        *used = remainder;
        buffer[remainder] = '\0';
    }
}

bool champions_consume_live_detail(char *buffer, size_t *used, champions_match_t *match)
{
    if (!buffer || !used || !match) return false;
    buffer[*used] = '\0';
    if (strstr(buffer, "Bo5")) match->best_of = 5;
    else if (strstr(buffer, "Bo3") && match->best_of == 0) match->best_of = 3;

    char *game = strstr(buffer, "<div class=\"vm-stats-game mod-active\"");
    if (!game) {
        size_t keep = *used < 64 ? *used : 64;
        memmove(buffer, buffer + *used - keep, keep);
        *used = keep;
        buffer[keep] = '\0';
        return false;
    }
    char *header = strstr(game, "<div class=\"vm-stats-game-header\"");
    if (!header) goto keep_game;
    char *score = header;
    int rounds[2];
    for (int i = 0; i < 2; ++i) {
        score = strstr(score, "class=\"score ");
        if (!score || (score = strchr(score, '>')) == NULL) goto keep_game;
        ++score;
        while (*score && isspace((unsigned char)*score)) ++score;
        if (!isdigit((unsigned char)*score)) goto keep_game;
        rounds[i] = atoi(score);
    }
    match->round_a = rounds[0];
    match->round_b = rounds[1];
    match->has_round_score = true;
    return true;

keep_game:
    if (game != buffer) {
        size_t remaining = *used - (size_t)(game - buffer);
        memmove(buffer, game, remaining);
        *used = remaining;
        buffer[remaining] = '\0';
    }
    return false;
}

int champions_home_match(const champions_snapshot_t *snapshot, int64_t now_utc)
{
    if (!snapshot) return -1;
    for (size_t i = 0; i < snapshot->count; ++i) {
        if (snapshot->matches[i].status == CHAMPIONS_LIVE) return (int)i;
    }
    int selected = -1;
    for (size_t i = 0; i < snapshot->count; ++i) {
        const champions_match_t *m = &snapshot->matches[i];
        if (m->status != CHAMPIONS_UPCOMING || m->start_utc < now_utc) continue;
        if (selected < 0 || m->start_utc < snapshot->matches[selected].start_utc) selected = (int)i;
    }
    if (selected >= 0) return selected;
    for (size_t i = 0; i < snapshot->count; ++i) {
        if (snapshot->matches[i].status == CHAMPIONS_FINISHED &&
            now_utc >= snapshot->matches[i].start_utc &&
            now_utc - snapshot->matches[i].start_utc <= 3 * 3600 &&
            (selected < 0 || snapshot->matches[i].start_utc > snapshot->matches[selected].start_utc)) selected = (int)i;
    }
    return selected;
}

int champions_live_match(const champions_snapshot_t *snapshot)
{
    if (!snapshot) return -1;
    for (size_t i = 0; i < snapshot->count; ++i)
        if (snapshot->matches[i].status == CHAMPIONS_LIVE) return (int)i;
    return -1;
}

int champions_schedule_delay(const champions_snapshot_t *snapshot, int64_t now_utc)
{
    if (champions_live_match(snapshot) >= 0) return 60;
    int home = champions_home_match(snapshot, now_utc);
    return home >= 0 && snapshot->matches[home].start_utc - now_utc <= 3600 ? 120 : 600;
}

void champions_keep_rounds(champions_snapshot_t *updated, const champions_snapshot_t *previous)
{
    if (!updated || !previous) return;
    for (size_t i = 0; i < updated->count; ++i) {
        champions_match_t *match = &updated->matches[i];
        if (match->status != CHAMPIONS_LIVE) continue;
        for (size_t j = 0; j < previous->count; ++j) {
            const champions_match_t *old = &previous->matches[j];
            if (match->id != old->id || old->status != CHAMPIONS_LIVE ||
                match->score_a != old->score_a || match->score_b != old->score_b) continue;
            if (!match->best_of) match->best_of = old->best_of;
            if (!match->has_round_score && old->has_round_score) {
                match->round_a = old->round_a;
                match->round_b = old->round_b;
                match->has_round_score = true;
            }
            break;
        }
    }
}

int champions_retry_delay(int previous_seconds)
{
    if (previous_seconds < 15) return 15;
    if (previous_seconds >= 120) return 120;
    return previous_seconds * 2;
}

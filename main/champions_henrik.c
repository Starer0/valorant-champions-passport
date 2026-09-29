#include "champions_henrik.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum { MODE_NORMAL, MODE_STRING, MODE_NUMBER, MODE_LITERAL };

static void scalar(champions_henrik_parser_t *p)
{
    p->token[p->token_used] = '\0';
    if (p->token_is_key) {
        size_t length = (size_t)p->token_used;
        if (length >= sizeof(p->frames[0].key)) length = sizeof(p->frames[0].key) - 1;
        memcpy(p->frames[p->depth - 1].key, p->token, length);
        p->frames[p->depth - 1].key[length] = '\0';
        p->frames[p->depth - 1].expect_key = false;
        return;
    }
    const char *key = p->depth ? p->frames[p->depth - 1].key : "";
    const char *value = p->token;
    if (p->depth == 1 && strcmp(key, "status") == 0) {
        p->root_ok = strcmp(value, "200") == 0;
    } else if (p->depth == 3 && strcmp(p->frames[1].name, "data") == 0 &&
               strcmp(p->frames[2].name, "metadata") == 0) {
        if (strcmp(key, "format") == 0) {
            if (strstr(value, "Bo5")) p->result.best_of = 5;
            else if (strstr(value, "Bo3")) p->result.best_of = 3;
        } else if (strcmp(key, "status") == 0) {
            if (strcmp(value, "live") == 0) p->result.status = CHAMPIONS_LIVE;
            else if (strcmp(value, "completed") == 0) p->result.status = CHAMPIONS_FINISHED;
            else p->result.status = CHAMPIONS_UPCOMING;
        }
    } else if (p->depth == 4 && strcmp(p->frames[2].name, "teams") == 0 &&
               strcmp(key, "score") == 0 && isdigit((unsigned char)value[0])) {
        int index = p->frames[2].index;
        if (index == 0) p->result.score_a = atoi(value);
        else if (index == 1) p->result.score_b = atoi(value);
    } else if (p->depth == 6 && strcmp(p->frames[2].name, "games") == 0 &&
               strcmp(p->frames[4].name, "teams") == 0 && strcmp(key, "score") == 0 &&
               isdigit((unsigned char)value[0])) {
        int game = p->frames[2].index;
        int team = p->frames[4].index;
        if (team >= 0 && team < 2) {
            if (game != p->current_game) {
                p->current_game = game;
                p->game_score_seen[0] = p->game_score_seen[1] = false;
            }
            p->game_score[team] = atoi(value);
            p->game_score_seen[team] = true;
            if (p->game_score_seen[0] && p->game_score_seen[1] &&
                (game == 0 || p->game_score[0] + p->game_score[1] > 0)) {
                p->result.round_a = p->game_score[0];
                p->result.round_b = p->game_score[1];
                p->result.has_round_score = true;
            }
        }
    }
}

void champions_henrik_init(champions_henrik_parser_t *p, const champions_match_t *base)
{
    *p = (champions_henrik_parser_t){0};
    if (base) p->result = *base;
    p->current_game = -1;
    p->result.has_round_score = false;
}

static void push(champions_henrik_parser_t *p, bool array)
{
    if (p->depth == 16) { p->invalid = true; return; }
    char name[24] = {0};
    if (p->depth && !p->frames[p->depth - 1].array)
        memcpy(name, p->frames[p->depth - 1].key, sizeof(name));
    memset(&p->frames[p->depth], 0, sizeof(p->frames[0]));
    memcpy(p->frames[p->depth].name, name, sizeof(name));
    p->frames[p->depth].array = array;
    p->frames[p->depth].expect_key = !array;
    ++p->depth;
}

bool champions_henrik_feed(champions_henrik_parser_t *p, const char *bytes, size_t length)
{
    if (!p || !bytes || p->invalid) return false;
    for (size_t i = 0; i < length && !p->invalid; ++i) {
        char ch = bytes[i];
        if (p->mode == MODE_STRING) {
            if (p->escaped) { p->escaped = false; continue; }
            if (ch == '\\') { p->escaped = true; continue; }
            if (ch == '"') { scalar(p); p->mode = MODE_NORMAL; continue; }
            if (p->token_used < (int)sizeof(p->token) - 1) p->token[p->token_used++] = ch;
            continue;
        }
        if (p->mode == MODE_NUMBER || p->mode == MODE_LITERAL) {
            if (ch == ',' || ch == '}' || ch == ']' || isspace((unsigned char)ch)) {
                scalar(p);
                p->mode = MODE_NORMAL;
            } else {
                if (p->token_used < (int)sizeof(p->token) - 1) p->token[p->token_used++] = ch;
                continue;
            }
        }
        if (isspace((unsigned char)ch) || ch == ':') continue;
        if (ch == '{' || ch == '[') push(p, ch == '[');
        else if (ch == '}' || ch == ']') {
            if (!p->depth || p->frames[p->depth - 1].array != (ch == ']')) p->invalid = true;
            else {
                --p->depth;
                if (!p->depth) p->complete = true;
            }
        } else if (ch == ',') {
            if (!p->depth) p->invalid = true;
            else if (p->frames[p->depth - 1].array) ++p->frames[p->depth - 1].index;
            else {
                p->frames[p->depth - 1].key[0] = '\0';
                p->frames[p->depth - 1].expect_key = true;
            }
        } else if (ch == '"') {
            p->mode = MODE_STRING;
            p->token_used = 0;
            p->token_is_key = p->depth && !p->frames[p->depth - 1].array &&
                              p->frames[p->depth - 1].expect_key;
        } else if (ch == '-' || isdigit((unsigned char)ch) || ch == 't' || ch == 'f' || ch == 'n') {
            p->mode = ch == '-' || isdigit((unsigned char)ch) ? MODE_NUMBER : MODE_LITERAL;
            p->token_used = 0;
            p->token_is_key = false;
            p->token[p->token_used++] = ch;
        } else p->invalid = true;
    }
    return !p->invalid;
}

bool champions_henrik_finish(const champions_henrik_parser_t *p, champions_match_t *out)
{
    if (!p || !out || p->invalid || !p->complete || !p->root_ok) return false;
    *out = p->result;
    return true;
}

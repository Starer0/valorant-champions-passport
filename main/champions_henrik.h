#pragma once

#include "champions_model.h"

typedef struct {
    struct {
        char name[24];
        char key[24];
        int index;
        bool array;
        bool expect_key;
    } frames[16];
    char token[96];
    int depth;
    int token_used;
    int mode;
    int current_game;
    int game_score[2];
    bool game_score_seen[2];
    bool root_ok;
    bool complete;
    bool invalid;
    bool token_is_key;
    bool escaped;
    champions_match_t result;
} champions_henrik_parser_t;

void champions_henrik_init(champions_henrik_parser_t *parser, const champions_match_t *base);
bool champions_henrik_feed(champions_henrik_parser_t *parser, const char *bytes, size_t length);
bool champions_henrik_finish(const champions_henrik_parser_t *parser, champions_match_t *out);

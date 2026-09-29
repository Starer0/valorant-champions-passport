#include "champions_henrik.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_live_match_stream_uses_latest_played_map(void)
{
    const char *json =
        "{\"status\":200,\"data\":{\"metadata\":{\"status\":\"live\",\"format\":\"Bo3\"},"
        "\"teams\":[{\"name\":\"100 Thieves\",\"score\":1},{\"name\":\"T1\",\"score\":0}],"
        "\"streams\":[{\"name\":\"stream\",\"link\":\"https://example.com/a\"}],"
        "\"games\":["
        "{\"map\":\"Summit\",\"teams\":[{\"score\":13},{\"score\":8}],\"rounds\":[]},"
        "{\"map\":\"Ascent\",\"teams\":[{\"score\":4},{\"score\":6}],\"rounds\":[]},"
        "{\"map\":\"Sunset\",\"teams\":[{\"score\":null},{\"score\":null}],\"rounds\":[]}]}}";
    champions_match_t base = {.id = 753444};
    champions_henrik_parser_t parser;
    champions_henrik_init(&parser, &base);
    for (size_t offset = 0; offset < strlen(json); offset += 7) {
        size_t n = strlen(json) - offset;
        if (n > 7) n = 7;
        assert(champions_henrik_feed(&parser, json + offset, n));
    }
    champions_match_t match;
    assert(champions_henrik_finish(&parser, &match));
    assert(match.id == 753444 && match.status == CHAMPIONS_LIVE);
    assert(match.best_of == 3 && match.score_a == 1 && match.score_b == 0);
    assert(match.has_round_score && match.round_a == 4 && match.round_b == 6);
}

static void test_rejects_failed_or_incomplete_response(void)
{
    const char *json = "{\"status\":401,\"data\":{\"metadata\":{\"status\":\"live\"}}}";
    champions_henrik_parser_t parser;
    champions_henrik_init(&parser, NULL);
    assert(champions_henrik_feed(&parser, json, strlen(json)));
    champions_match_t match;
    assert(!champions_henrik_finish(&parser, &match));
    champions_henrik_init(&parser, NULL);
    assert(champions_henrik_feed(&parser, json, strlen(json) - 2));
    assert(!champions_henrik_finish(&parser, &match));
}

static void test_bo5_format_and_final_score(void)
{
    const char *json = "{\"data\":{\"games\":[],\"teams\":[{\"score\":3},{\"score\":2}],"
                       "\"metadata\":{\"format\":\"Bo5\",\"status\":\"completed\"}},\"status\":200}";
    champions_henrik_parser_t parser;
    champions_henrik_init(&parser, NULL);
    assert(champions_henrik_feed(&parser, json, strlen(json)));
    champions_match_t match;
    assert(champions_henrik_finish(&parser, &match));
    assert(match.best_of == 5 && match.status == CHAMPIONS_FINISHED);
    assert(match.score_a == 3 && match.score_b == 2);
}

int main(void)
{
    test_live_match_stream_uses_latest_played_map();
    test_rejects_failed_or_incomplete_response();
    test_bo5_format_and_final_score();
    puts("champions HenrikDev parser: PASS");
    return 0;
}

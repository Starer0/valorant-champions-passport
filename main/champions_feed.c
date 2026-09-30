#include "champions_feed.h"
#include "champions_henrik.h"
#include "passport_net.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "champions_feed";
static const char *CALENDAR_URL = "https://www.vlr.gg/event/ical/2766";
static const char *MATCHES_URL = "https://www.vlr.gg/event/matches/2766/valorant-champions-2026/?group=all&series_id=all";
static char s_calendar[32769];
static char s_match_buffer[8193];
static champions_snapshot_t s_updated_snapshot;
static champions_henrik_parser_t s_henrik_parser;

static esp_http_client_handle_t open_get(const char *url, const char *api_key)
{
    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 1024,
        .user_agent = "FoloToy-Passport-VCT/1.0 (personal schedule display)",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return NULL;
    if (api_key && esp_http_client_set_header(client, "Authorization", api_key) != ESP_OK) {
        esp_http_client_cleanup(client);
        return NULL;
    }
    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return NULL;
    }
    esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200) {
        ESP_LOGW(TAG, "HTTP %d: %s", esp_http_client_get_status_code(client), url);
        esp_http_client_cleanup(client);
        return NULL;
    }
    return client;
}

static esp_err_t fetch_calendar(champions_snapshot_t *snapshot)
{
    esp_http_client_handle_t client = open_get(CALENDAR_URL, NULL);
    if (!client) return ESP_FAIL;
    size_t used = 0;
    int count;
    while (used < sizeof(s_calendar) - 1 &&
           (count = esp_http_client_read(client, s_calendar + used,
                                         sizeof(s_calendar) - 1 - used)) > 0) {
        used += (size_t)count;
    }
    esp_http_client_cleanup(client);
    if (used == sizeof(s_calendar) - 1) return ESP_ERR_INVALID_SIZE;
    s_calendar[used] = '\0';
    if (!champions_parse_ics(s_calendar, used, snapshot)) return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

static esp_err_t fetch_scores(champions_snapshot_t *snapshot)
{
    esp_http_client_handle_t client = open_get(MATCHES_URL, NULL);
    if (!client) return ESP_FAIL;
    char *buffer = s_match_buffer;
    size_t used = 0;
    int count;
    esp_err_t result = ESP_OK;
    while ((count = esp_http_client_read(client, buffer + used,
                                         sizeof(s_match_buffer) - 1 - used)) > 0) {
        used += (size_t)count;
        champions_consume_html(buffer, &used, snapshot);
        if (used == sizeof(s_match_buffer) - 1) {
            ESP_LOGW(TAG, "Match block exceeded parser buffer");
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
    }
    if (count < 0) result = ESP_FAIL;
    esp_http_client_cleanup(client);
    return result;
}

static esp_err_t fetch_live_rounds(champions_match_t *match)
{
    char url[64];
    snprintf(url, sizeof(url), "https://www.vlr.gg/%d/", match->id);
    esp_http_client_handle_t client = open_get(url, NULL);
    if (!client) return ESP_FAIL;
    size_t used = 0;
    int count = 0;
    bool found = false;
    esp_err_t result = ESP_OK;
    while ((count = esp_http_client_read(client, s_match_buffer + used,
                                         sizeof(s_match_buffer) - 1 - used)) > 0) {
        used += (size_t)count;
        if (champions_consume_live_detail(s_match_buffer, &used, match)) {
            found = true;
            break;
        }
        if (used == sizeof(s_match_buffer) - 1) {
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
    }
    if (count < 0) result = ESP_FAIL;
    esp_http_client_cleanup(client);
    if (result == ESP_OK && !found) result = ESP_ERR_NOT_FOUND;
    return result;
}

static esp_err_t fetch_match_start(champions_match_t *match)
{
    char url[64];
    snprintf(url, sizeof(url), "https://www.vlr.gg/%d/", match->id);
    esp_http_client_handle_t client = open_get(url, NULL);
    if (!client) return ESP_FAIL;
    size_t used = 0, received = 0;
    esp_err_t result = ESP_ERR_NOT_FOUND;
    int count;
    while (received < 65536 &&
           (count = esp_http_client_read(client, s_match_buffer + used, 1024)) > 0) {
        used += (size_t)count;
        received += (size_t)count;
        s_match_buffer[used] = '\0';
        if (champions_parse_match_start(s_match_buffer, used, match)) {
            result = ESP_OK;
            break;
        }
        size_t keep = used < 64 ? used : 64;
        memmove(s_match_buffer, s_match_buffer + used - keep, keep);
        used = keep;
    }
    esp_http_client_cleanup(client);
    return result;
}

static esp_err_t restore_missing_start_times(champions_snapshot_t *snapshot)
{
    int requests = 0;
    for (size_t i = 0; i < snapshot->count; ++i) {
        champions_match_t *match = &snapshot->matches[i];
        if (match->start_utc > 0) continue;
        if (++requests > 4) return ESP_ERR_INVALID_SIZE;
        esp_err_t result = fetch_match_start(match);
        if (result != ESP_OK) return result;
        if (snapshot->has_recent_final && snapshot->recent_final.id == match->id)
            snapshot->recent_final.start_utc = match->start_utc;
    }
    return ESP_OK;
}

static esp_err_t fetch_henrik_detail(champions_match_t *match)
{
    char key[128];
    if (!passport_net_api_key(key, sizeof(key))) return ESP_ERR_NOT_FOUND;
    char url[96];
    snprintf(url, sizeof(url),
             "https://api.henrikdev.xyz/valorant/v2/esports/vlr/matches/%d", match->id);
    esp_http_client_handle_t client = open_get(url, key);
    memset(key, 0, sizeof(key));
    if (!client) return ESP_FAIL;
    champions_henrik_init(&s_henrik_parser, match);
    int count;
    esp_err_t result = ESP_OK;
    while ((count = esp_http_client_read(client, s_match_buffer, 1024)) > 0) {
        if (!champions_henrik_feed(&s_henrik_parser, s_match_buffer, (size_t)count)) {
            result = ESP_ERR_INVALID_RESPONSE;
            break;
        }
    }
    if (count < 0) result = ESP_FAIL;
    esp_http_client_cleanup(client);
    champions_match_t parsed;
    if (result == ESP_OK && champions_henrik_finish(&s_henrik_parser, &parsed)) {
        *match = parsed;
        return ESP_OK;
    }
    return result == ESP_OK ? ESP_ERR_INVALID_RESPONSE : result;
}

static champions_match_t *current_candidate(champions_snapshot_t *snapshot,
                                            const champions_snapshot_t *previous)
{
    if (previous) {
        for (size_t i = 0; i < previous->count; ++i) {
            if (previous->matches[i].status != CHAMPIONS_LIVE) continue;
            for (size_t j = 0; j < snapshot->count; ++j)
                if (snapshot->matches[j].id == previous->matches[i].id)
                    return &snapshot->matches[j];
        }
    }
    int64_t now = time(NULL);
    champions_match_t *candidate = NULL;
    for (size_t i = 0; i < snapshot->count; ++i) {
        champions_match_t *match = &snapshot->matches[i];
        if (match->start_utc > now || now - match->start_utc > 8 * 3600) continue;
        if (!candidate || match->start_utc > candidate->start_utc) candidate = match;
    }
    return candidate;
}

esp_err_t champions_feed_refresh(champions_snapshot_t *snapshot)
{
    if (!snapshot) return ESP_ERR_INVALID_ARG;
    champions_snapshot_t *updated = &s_updated_snapshot;
    memset(updated, 0, sizeof(*updated));
    esp_err_t result = fetch_calendar(updated);
    if (result != ESP_OK) return result;
    champions_keep_schedule(updated, snapshot, time(NULL));
    int64_t score_started = esp_timer_get_time();
    esp_err_t score_result = fetch_scores(updated);
    ESP_LOGI(TAG, "VLR match list: %s in %lld ms", esp_err_to_name(score_result),
             (long long)((esp_timer_get_time() - score_started) / 1000));
    if (score_result != ESP_OK) {
        ESP_LOGW(TAG, "Score fetch unavailable: %s", esp_err_to_name(score_result));
        champions_match_t *candidate = current_candidate(updated, snapshot);
        if (!candidate || fetch_henrik_detail(candidate) != ESP_OK) return score_result;
        ESP_LOGI(TAG, "Using HenrikDev match backup");
        champions_keep_rounds(updated, snapshot);
        *snapshot = *updated;
        return ESP_OK;
    }
    result = restore_missing_start_times(updated);
    if (result != ESP_OK) return result;
    champions_keep_rounds(updated, snapshot);
    *snapshot = *updated;
    return ESP_OK;
}

esp_err_t champions_feed_refresh_rounds(champions_snapshot_t *snapshot)
{
    int index = champions_live_match(snapshot);
    if (index < 0) return ESP_ERR_NOT_FOUND;
    champions_match_t updated = snapshot->matches[index];
    int64_t started = esp_timer_get_time();
    esp_err_t result = fetch_live_rounds(&updated);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Live rounds unavailable: %s", esp_err_to_name(result));
        result = fetch_henrik_detail(&updated);
        if (result == ESP_OK) {
            if ((updated.status == CHAMPIONS_LIVE && !updated.has_round_score) ||
                updated.status == CHAMPIONS_UPCOMING) result = ESP_ERR_INVALID_RESPONSE;
            else ESP_LOGI(TAG, "Using HenrikDev round backup");
        }
    }
    ESP_LOGI(TAG, "Round refresh: %s in %lld ms", esp_err_to_name(result),
             (long long)((esp_timer_get_time() - started) / 1000));
    if (result == ESP_OK) snapshot->matches[index] = updated;
    return result;
}

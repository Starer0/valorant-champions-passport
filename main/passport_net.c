#include "passport_net.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/apps/sntp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static httpd_handle_t s_httpd;
static esp_netif_t *s_ap_netif;
static volatile bool s_online;
static volatile bool s_setup_requested;
static volatile bool s_portal_active;
static volatile bool s_manual_portal;
static volatile passport_net_state_t s_state = PASSPORT_NET_CONNECTING;
static int s_retries;
static char s_ap_name[24];
static char s_ap_password[12];
static bool s_sntp_started;

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *out, size_t capacity, const char *in)
{
    size_t n = 0;
    while (*in && n + 1 < capacity) {
        if (*in == '%' && in[1] && in[2]) {
            int a = hex_digit(in[1]);
            int b = hex_digit(in[2]);
            if (a >= 0 && b >= 0) {
                out[n++] = (char)(a * 16 + b);
                in += 3;
                continue;
            }
        }
        out[n++] = *in == '+' ? ' ' : *in;
        ++in;
    }
    out[n] = '\0';
}

static esp_err_t page_get(httpd_req_t *req)
{
    static const char page[] =
        "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Passport VCT Wi-Fi</title></head><body style='font:18px sans-serif;max-width:32em;margin:2em auto;padding:1em'>"
        "<h1>Passport VCT</h1><p>Connect this device to a 2.4 GHz Wi-Fi network.</p>"
        "<form method=post action=/save><label>Wi-Fi name<br><input name=ssid required maxlength=32></label><br><br>"
        "<label>Password<br><input name=password type=password maxlength=63></label><br><br>"
        "<button type=submit>Connect</button></form>"
        "<hr><h2>HenrikDev backup</h2>"
        "<form method=post action=/api-key><label>API key<br>"
        "<input name=api_key type=password required maxlength=127 autocomplete=off></label><br><br>"
        "<button type=submit>Save API key</button></form>"
        "<form method=post action=/clear-api-key><button type=submit>Remove API key</button></form>"
        "<form method=post action=/forget><button type=submit>Forget saved Wi-Fi</button></form>"
        "<p>After saving, return to the Passport screen for connection status.</p></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_key_post(httpd_req_t *req)
{
    if (req->content_len < 9 || req->content_len > 256) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid key size");
        return ESP_FAIL;
    }
    char body[257] = {0};
    char key[128] = {0};
    int used = 0;
    while (used < req->content_len) {
        int n = httpd_req_recv(req, body + used, req->content_len - used);
        if (n <= 0) return ESP_FAIL;
        used += n;
    }
    if (strncmp(body, "api_key=", 8) == 0) url_decode(key, sizeof(key), body + 8);
    size_t length = strlen(key);
    bool valid = length >= 8 && length < sizeof(key) - 1;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)key[i] < 33 || (unsigned char)key[i] > 126) valid = false;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (valid) {
        nvs_handle_t handle;
        err = nvs_open("vct_api", NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_str(handle, "henrik", key);
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
        }
    }
    memset(key, 0, sizeof(key));
    memset(body, 0, sizeof(body));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Could not save API key");
        return err;
    }
    s_manual_portal = false;
    s_state = s_online ? PASSPORT_NET_ONLINE : PASSPORT_NET_CONNECTING;
    return httpd_resp_sendstr(req, "API key saved. Return to the Passport screen.");
}

static esp_err_t clear_api_key_post(httpd_req_t *req)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("vct_api", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_erase_key(handle, "henrik");
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not remove API key");
        return err;
    }
    return httpd_resp_sendstr(req, "API key removed.");
}

static esp_err_t save_post(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 256) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form size");
        return ESP_FAIL;
    }
    char body[257] = {0};
    int used = 0;
    while (used < req->content_len) {
        int n = httpd_req_recv(req, body + used, req->content_len - used);
        if (n <= 0) return ESP_FAIL;
        used += n;
    }
    char ssid[33] = {0};
    char password[64] = {0};
    char *save = NULL;
    for (char *field = strtok_r(body, "&", &save); field; field = strtok_r(NULL, "&", &save)) {
        char *eq = strchr(field, '=');
        if (!eq) continue;
        *eq++ = '\0';
        if (strcmp(field, "ssid") == 0) url_decode(ssid, sizeof(ssid), eq);
        else if (strcmp(field, "password") == 0) url_decode(password, sizeof(password), eq);
    }
    if (!ssid[0] || strlen(ssid) > 32 || strlen(password) > 63) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Wi-Fi name or password");
        return ESP_FAIL;
    }
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    memset(password, 0, sizeof(password));
    memset(body, 0, sizeof(body));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save Wi-Fi");
        return err;
    }
    s_retries = 0;
    s_state = PASSPORT_NET_CONNECTING;
    esp_wifi_disconnect();
    esp_wifi_connect();
    return httpd_resp_sendstr(req, "Saved. Check the Passport screen for connection status.");
}

static esp_err_t forget_post(httpd_req_t *req)
{
    wifi_config_t empty = {0};
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &empty);
    if (err == ESP_OK) {
        s_online = false;
        s_retries = 0;
        s_state = PASSPORT_NET_SETUP;
        return httpd_resp_sendstr(req, "Saved Wi-Fi removed. Enter another network.");
    }
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not remove Wi-Fi");
    return err;
}

static esp_err_t start_portal(void)
{
    if (s_portal_active) {
        s_state = PASSPORT_NET_SETUP;
        return ESP_OK;
    }
    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) return ESP_ERR_NO_MEM;
    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "%s", s_ap_name);
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "%s", s_ap_password);
    ap.ap.ssid_len = strlen(s_ap_name);
    ap.ap.channel = 1;
    ap.ap.max_connection = 1;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err != ESP_OK) return err;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 5;
    err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = page_get};
    const httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_post};
    const httpd_uri_t forget = {.uri = "/forget", .method = HTTP_POST, .handler = forget_post};
    const httpd_uri_t api_key = {.uri = "/api-key", .method = HTTP_POST, .handler = api_key_post};
    const httpd_uri_t clear_api_key = {.uri = "/clear-api-key", .method = HTTP_POST, .handler = clear_api_key_post};
    httpd_register_uri_handler(s_httpd, &root);
    httpd_register_uri_handler(s_httpd, &save);
    httpd_register_uri_handler(s_httpd, &forget);
    httpd_register_uri_handler(s_httpd, &api_key);
    httpd_register_uri_handler(s_httpd, &clear_api_key);
    s_portal_active = true;
    s_state = PASSPORT_NET_SETUP;
    return ESP_OK;
}

static void stop_portal(void)
{
    if (!s_portal_active) return;
    httpd_stop(s_httpd);
    s_httpd = NULL;
    s_portal_active = false;
    esp_wifi_set_mode(WIFI_MODE_STA);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t event, void *data)
{
    (void)arg; (void)base; (void)data;
    if (event == WIFI_EVENT_STA_START) {
        wifi_config_t saved = {0};
        if (esp_wifi_get_config(WIFI_IF_STA, &saved) == ESP_OK && saved.sta.ssid[0]) {
            s_state = PASSPORT_NET_CONNECTING;
            esp_wifi_connect();
        } else {
            s_setup_requested = true;
        }
    } else if (event == WIFI_EVENT_STA_DISCONNECTED) {
        s_online = false;
        if (++s_retries <= 5) {
            s_state = PASSPORT_NET_CONNECTING;
            esp_wifi_connect();
        } else {
            s_setup_requested = true;
        }
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t event, void *data)
{
    (void)arg; (void)base; (void)data;
    if (event == IP_EVENT_STA_GOT_IP) {
        s_online = true;
        s_manual_portal = false;
        s_retries = 0;
        s_state = PASSPORT_NET_ONLINE;
    }
}

esp_err_t passport_net_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err != ESP_OK) return err;
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(s_ap_name, sizeof(s_ap_name), "Passport-VCT-%02X%02X", mac[4], mac[5]);
    snprintf(s_ap_password, sizeof(s_ap_password), "%08lu", (unsigned long)(esp_random() % 100000000));
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event, NULL);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;
    return ESP_OK;
}

void passport_net_service(void)
{
    if (s_setup_requested) {
        s_setup_requested = false;
        if (start_portal() != ESP_OK) s_state = PASSPORT_NET_ERROR;
    }
    if (s_online && !s_manual_portal) {
        stop_portal();
        if (!s_sntp_started) {
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_setservername(0, "pool.ntp.org");
            sntp_init();
            s_sntp_started = true;
        }
    }
}

void passport_net_request_setup(void)
{
    s_manual_portal = true;
    s_setup_requested = true;
}
bool passport_net_online(void) { return s_online; }
passport_net_state_t passport_net_state(void) { return s_state; }
const char *passport_net_ap_name(void) { return s_ap_name; }
const char *passport_net_ap_password(void) { return s_ap_password; }
bool passport_net_api_key(char *out, size_t capacity)
{
    if (!out || capacity == 0) return false;
    out[0] = '\0';
    nvs_handle_t handle;
    if (nvs_open("vct_api", NVS_READONLY, &handle) != ESP_OK) return false;
    size_t length = capacity;
    esp_err_t err = nvs_get_str(handle, "henrik", out, &length);
    nvs_close(handle);
    if (err != ESP_OK) { out[0] = '\0'; return false; }
    return true;
}

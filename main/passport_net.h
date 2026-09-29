#pragma once

#include <stddef.h>

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    PASSPORT_NET_CONNECTING = 0,
    PASSPORT_NET_SETUP,
    PASSPORT_NET_ONLINE,
    PASSPORT_NET_ERROR,
} passport_net_state_t;

esp_err_t passport_net_init(void);
void passport_net_service(void);
void passport_net_request_setup(void);
bool passport_net_online(void);
passport_net_state_t passport_net_state(void);
const char *passport_net_ap_name(void);
const char *passport_net_ap_password(void);
bool passport_net_api_key(char *out, size_t capacity);


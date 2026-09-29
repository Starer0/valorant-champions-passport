idf_component_register(
    SRCS "champions_app.c" "champions_model.c" "champions_feed.c" "champions_henrik.c" "passport_net.c"
         "champions_assets.c"
    INCLUDE_DIRS "."
    EMBED_FILES "champions_assets.bin"
    REQUIRES bsp esp_event esp_http_client esp_http_server esp_netif esp_timer esp_wifi
             nvs_flash mbedtls lwip
)

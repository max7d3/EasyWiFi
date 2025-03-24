idf_component_register(SRCS "easy_wifi.c"
                       INCLUDE_DIRS "inc"
                       PRIV_REQUIRES driver
                       REQUIRES esp_wifi
                       REQUIRES nvs_flash
                       REQUIRES esp_http_server
                       EMBED_TXTFILES "portal.html")
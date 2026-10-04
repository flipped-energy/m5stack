idf_component_register(SRCS "src/session.cpp" "src/server.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES flipped_core esp_common
                       PRIV_REQUIRES flipped_app esp_http_server esp_hw_support mbedtls log
                       EMBED_TXTFILES "web/token.html")

idf_component_register(SRCS "src/http_client.cpp" "src/sntp.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES flipped_core freertos
                       PRIV_REQUIRES esp_http_client esp-tls mbedtls esp_netif lwip log)

idf_component_register(SRCS "src/app_task.cpp" "src/timers.cpp" "src/esp_platform.cpp" "src/console.cpp" "src/time_zone_check.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES flipped_core flipped_matter freertos
                       PRIV_REQUIRES flipped_net flipped_store esp_matter esp_matter_console chip esp_timer esp_wifi
                                     esp_netif esp_event esp_app_format heap nvs_flash log pthread)

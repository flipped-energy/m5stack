idf_component_register(SRCS "src/config_store.cpp" "src/eve_history_store.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES flipped_core nvs_flash)

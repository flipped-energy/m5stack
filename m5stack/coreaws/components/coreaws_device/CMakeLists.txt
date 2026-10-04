idf_component_register(
    SRCS "src/board.cpp" "src/renderer.cpp" "src/device.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES flipped_ui_core nvs_flash esp_timer esp_driver_gpio mbedtls
    EMBED_FILES "assets/flipped_wordmark.png" "assets/flipped_bolt.png" "assets/flipped_header.png" "assets/flipped_header_bolt.png")

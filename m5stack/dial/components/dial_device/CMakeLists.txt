idf_component_register(
    SRCS "src/knob.cpp" "src/round.cpp" "src/device.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES flipped_ui_core esp_timer esp_driver_gpio mbedtls
    EMBED_FILES "assets/flipped_wordmark.png" "assets/flipped_bolt.png" "assets/flipped_logo.png")

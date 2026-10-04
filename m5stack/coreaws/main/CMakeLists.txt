idf_component_register(SRCS "app_main.cpp"
                       PRIV_INCLUDE_DIRS "."
                       PRIV_REQUIRES coreaws_device flipped_app flipped_matter flipped_store flipped_core flipped_ui_app flipped_dev_wifi flipped_net heap nvs_flash freertos)

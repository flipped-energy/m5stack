idf_component_register(SRCS "src/app_bridge.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES flipped_ui_core flipped_app
                       PRIV_REQUIRES flipped_provision log)

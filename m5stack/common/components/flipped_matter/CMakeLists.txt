idf_component_register(SRCS "src/node.cpp" "src/switches.cpp" "src/flipped_cluster.cpp" "src/commissionable_data.cpp"
                            "src/device_instance_info.cpp" "src/onboarding.cpp" "src/commissioning.cpp" "src/adapter.cpp"
                            "src/meter.cpp" "src/commodity_price.cpp" "src/commodity_tariff_server.cpp" "src/eve_history_cluster.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES flipped_core
                       PRIV_REQUIRES flipped_store esp_matter chip nvs_flash bootloader_support esp_hw_support esp_system log)

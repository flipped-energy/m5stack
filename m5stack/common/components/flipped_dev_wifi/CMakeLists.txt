set(dev_wifi_dir "${CMAKE_BINARY_DIR}/dev_wifi")
file(MAKE_DIRECTORY "${dev_wifi_dir}")
if(DEFINED ENV{FLIPPED_DEV_WIFI_1_SSID})
    set(networks "")
    foreach(n 1 2 3)
        if(DEFINED ENV{FLIPPED_DEV_WIFI_${n}_SSID})
            string(APPEND networks "    DevNetwork{\"$ENV{FLIPPED_DEV_WIFI_${n}_SSID}\", \"$ENV{FLIPPED_DEV_WIFI_${n}_PASSWORD}\"},\n")
        endif()
    endforeach()
    file(WRITE "${dev_wifi_dir}/dev_wifi_networks.h"
        "#pragma once\nstruct DevNetwork {\n    const char* ssid;\n    const char* password;\n};\ninline constexpr DevNetwork kDevNetworks[] = {\n${networks}};\n")
else()
    file(REMOVE "${dev_wifi_dir}/dev_wifi_networks.h")
endif()
idf_component_register(SRCS "src/dev_wifi.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "${dev_wifi_dir}"
                       PRIV_REQUIRES esp_wifi esp_event log freertos)

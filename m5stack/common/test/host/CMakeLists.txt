cmake_minimum_required(VERSION 3.16)
project(flipped_host_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED ARDUINOJSON_DIR)
    message(FATAL_ERROR "ARDUINOJSON_DIR is not set; run m5stack/common/test.sh <board>")
endif()
if(NOT EXISTS "${ARDUINOJSON_DIR}/src/ArduinoJson.h")
    message(FATAL_ERROR "${ARDUINOJSON_DIR}/src/ArduinoJson.h does not exist")
endif()

set(components "${CMAKE_CURRENT_LIST_DIR}/../../components")
file(GLOB core_sources CONFIGURE_DEPENDS "${components}/flipped_core/src/*.cpp")

add_library(arduinojson INTERFACE)
target_include_directories(arduinojson INTERFACE "${ARDUINOJSON_DIR}/src")
target_compile_definitions(arduinojson INTERFACE ARDUINOJSON_USE_DOUBLE=1 ARDUINOJSON_USE_LONG_LONG=1)

add_library(flipped_core STATIC ${core_sources})
target_include_directories(flipped_core PUBLIC "${components}/flipped_core/include")
target_link_libraries(flipped_core PRIVATE arduinojson)
target_compile_options(flipped_core PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)

add_executable(core_tests main.cpp vector_runner.cpp loop_tests.cpp ledger_tests.cpp identity_tests.cpp projection_tests.cpp config_tests.cpp switch_plan_tests.cpp tariff_tables_tests.cpp metering_tests.cpp)
target_include_directories(core_tests PRIVATE "${components}/flipped_core/src" "${components}/flipped_app/include")
target_link_libraries(core_tests PRIVATE flipped_core arduinojson)
target_compile_options(core_tests PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)

add_executable(eve_history_tests eve_history_tests.cpp)
target_include_directories(eve_history_tests PRIVATE "${components}/flipped_core/src")
target_link_libraries(eve_history_tests PRIVATE flipped_core arduinojson)
target_compile_options(eve_history_tests PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)

add_executable(provision_tests provision_tests.cpp "${components}/flipped_provision/src/session.cpp")
target_include_directories(provision_tests PRIVATE "${components}/flipped_provision/include" "${components}/flipped_core/include")
target_link_libraries(provision_tests PRIVATE arduinojson)
target_compile_options(provision_tests PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)

file(GLOB ui_core_sources CONFIGURE_DEPENDS "${components}/flipped_ui_core/src/*.cpp")
add_executable(ui_core_tests ui_core_tests.cpp ${ui_core_sources})
target_include_directories(ui_core_tests PRIVATE "${components}/flipped_ui_core/include")
target_link_libraries(ui_core_tests PRIVATE flipped_core)
target_compile_options(ui_core_tests PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)

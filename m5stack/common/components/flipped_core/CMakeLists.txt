idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src")
target_compile_definitions(${COMPONENT_LIB} PRIVATE ARDUINOJSON_USE_DOUBLE=1 ARDUINOJSON_USE_LONG_LONG=1)

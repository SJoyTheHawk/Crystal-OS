idf_component_register(
    SRCS "src/crystal_package.cpp" "src/crystal_resources.cpp"
    INCLUDE_DIRS "include"
    REQUIRES crystal_hal crystal_signature espressif__cjson joltwallet__littlefs
)

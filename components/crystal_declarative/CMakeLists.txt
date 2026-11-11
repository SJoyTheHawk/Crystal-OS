idf_component_register(
    SRCS "src/crystal_declarative.cpp"
    INCLUDE_DIRS "include"
    REQUIRES crystal_app crystal_package crystal_core crystal_hal espressif__cjson
)

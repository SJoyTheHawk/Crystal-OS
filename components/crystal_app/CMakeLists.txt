idf_component_register(
    SRCS "src/crystal_app.cpp" "src/crystal_ui.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp-brookesia crystal_hal nvs_flash crystal_package espressif__cjson
)

# Brookesia's public default-data macros intentionally leave optional style
# fields zero-initialized through aggregate initialization.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)

idf_component_register(
    SRCS "src/crystal_shell.cpp" "src/crystal_keyboard.cpp" "src/manage_apps_page.cpp"
         "src/crystal_stylesheet.cpp"
         "src/crystal_boot_screen.cpp"
         "src/crystal_boot_logo.cpp"
         "src/crystal_page_transition.cpp"
         "src/crystal_ui_metrics.cpp"
    INCLUDE_DIRS "include"
    REQUIRES crystal_registry crystal_hal crystal_app crystal_core crystal_control weather_app esp-brookesia lvgl freertos
)

# Pre-sized artwork avoids runtime PNG decoding/scaling and S3 flash growth.
if(IDF_TARGET STREQUAL "esp32p4")
    target_add_binary_data(${COMPONENT_LIB} "assets/crystalos_boot_logo_256.bin"
        BINARY RENAME_TO crystal_boot_logo_rgb565a8)
endif()

# Brookesia presets intentionally omit zero-valued aggregate members. Keep
# this exception local to the extracted stylesheet, as in main previously.
set_source_files_properties(src/crystal_stylesheet.cpp PROPERTIES
    COMPILE_OPTIONS "-Wno-missing-field-initializers")

idf_component_register(
    SRCS "main.cpp" "crystal_version.c" "crystal_crash.c" "crystal_update.c" "crystal_update_manifest.c"
    INCLUDE_DIRS "."
    REQUIRES dev_tester clock_app weather_app calculator_app crystal_hal crystal_core crystal_app crystal_registry crystal_package crystal_signature crystal_control crystal_shell esp-brookesia esp_lvgl_port esp32_s3_touch_lcd_4b nvs_flash esp_timer espcoredump esp_https_ota app_update mbedtls
)

target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-missing-field-initializers)

option(CRYSTAL_PHASE14_DEVICE_TEST "Run the Calculator package test on the device (one automatic reboot)" OFF)
set(CRYSTAL_PHASE14_DEVICE_TEST_RUN "1" CACHE STRING "Increase to repeat a completed Phase 14 device test")
if(CRYSTAL_PHASE14_DEVICE_TEST)
    string(LENGTH "${CRYSTAL_PHASE14_DEVICE_TEST_RUN}" phase14_run_length)
    if(NOT CRYSTAL_PHASE14_DEVICE_TEST_RUN MATCHES "^[1-9][0-9]*$" OR phase14_run_length GREATER 9)
        message(FATAL_ERROR "CRYSTAL_PHASE14_DEVICE_TEST_RUN must be a positive integer with at most 9 digits")
    endif()
    idf_build_get_property(phase14_python PYTHON)
    set(phase14_fixture "${CMAKE_CURRENT_BINARY_DIR}/phase14_calculator.capp")
    add_custom_command(OUTPUT "${phase14_fixture}"
        COMMAND "${phase14_python}" "${PROJECT_DIR}/tools/prepare_phase14_device_test.py" "${phase14_fixture}"
        DEPENDS "${PROJECT_DIR}/tools/prepare_phase14_device_test.py"
                "${PROJECT_DIR}/tools/crystal.py"
                "${PROJECT_DIR}/examples/calculator_package/app.yaml"
                "${PROJECT_DIR}/examples/calculator_package/calculator.native"
        VERBATIM)
    target_add_binary_data(${COMPONENT_LIB} "${phase14_fixture}" BINARY RENAME_TO phase14_calculator_capp)
    target_sources(${COMPONENT_LIB} PRIVATE "crystal_phase14_device_test.cpp")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CRYSTAL_PHASE14_DEVICE_TEST=1
        CRYSTAL_PHASE14_DEVICE_TEST_RUN=${CRYSTAL_PHASE14_DEVICE_TEST_RUN})
endif()

# Brookesia creates most UI objects. Route those allocations to octal PSRAM.
if(CONFIG_ESP_BROOKESIA_MEMORY_USE_CUSTOM)
    idf_build_get_property(build_components BUILD_COMPONENTS)
    foreach(component ${build_components})
        if(component MATCHES "esp-brookesia" OR component MATCHES "espressif__esp-brookesia")
            set(brookesia_component ${component})
            break()
        endif()
    endforeach()

    if(NOT DEFINED brookesia_component)
        message(FATAL_ERROR "Component 'esp-brookesia' not found")
    endif()

    idf_component_get_property(brookesia_lib ${brookesia_component} COMPONENT_LIB)
    target_compile_options(
        ${brookesia_lib}
        PUBLIC
            "-DESP_BROOKESIA_MEMORY_CUSTOM_MALLOC(x)=heap_caps_aligned_alloc(1, x, MALLOC_CAP_SPIRAM)"
            "-DESP_BROOKESIA_MEMORY_CUSTOM_FREE=free"
    )
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions")

spiffs_create_partition_image(
    storage
    ../assets
    FLASH_IN_PROJECT
    SPIFFS_IMAGE_FLAGS="-b 4096 -p 256 -s 0x200000 -q"
)

# A factory image is generated explicitly; normal boots never reformat apps.
littlefs_create_partition_image(apps ../apps_factory FLASH_IN_PROJECT)

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES hello_app state_test_app clock_app crystal_hal crystal_core crystal_app crystal_registry crystal_shell esp-brookesia esp_lvgl_port esp32_s3_touch_lcd_4b nvs_flash esp_timer
)

target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-missing-field-initializers)

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
    SPIFFS_IMAGE_FLAGS="-b 4096 -p 256 -s 0x400000 -q"
)

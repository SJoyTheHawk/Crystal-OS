if(NOT IDF_TARGET STREQUAL "esp32p4")
    return()
endif()

idf_component_register(
    SRCS "src/crystal_board_p4.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_board_manager esp_codec_dev esp_driver_i2c esp_driver_ledc esp_lvgl_adapter lvgl spiffs
)

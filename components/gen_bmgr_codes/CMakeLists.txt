if(NOT IDF_TARGET STREQUAL "esp32p4")
    return()
endif()

# Board information output
message(STATUS "Selected Board: esp32_p4_wifi6_touch_lcd_4b")
message(STATUS "Board Path: /Users/szemy/Workspace/ESP32 Crystal OS/managed_components/waveshare__waveshare_boards/boards/esp32_p4_wifi6_touch_lcd_4b")

idf_component_register(
    SRC_DIRS "." "../../managed_components/waveshare__waveshare_boards/boards/esp32_p4_wifi6_touch_lcd_4b"
    INCLUDE_DIRS "." "../../managed_components/waveshare__waveshare_boards/boards/esp32_p4_wifi6_touch_lcd_4b"
    REQUIRES esp_board_manager waveshare__waveshare_boards
)

# This is equivalent to adding WHOLE_ARCHIVE option to the idf_component_register call above:
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)

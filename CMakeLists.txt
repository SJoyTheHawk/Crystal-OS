cmake_minimum_required(VERSION 3.16)

if(IDF_TARGET STREQUAL "esp32p4")
    set(SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.common;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.esp32p4")
    set(p4_board_manager_defaults
        "${CMAKE_CURRENT_LIST_DIR}/components/gen_bmgr_codes/board_manager.defaults")
    if(EXISTS "${p4_board_manager_defaults}")
        list(APPEND SDKCONFIG_DEFAULTS "${p4_board_manager_defaults}")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(crystal_os VERSION 0.15.0)

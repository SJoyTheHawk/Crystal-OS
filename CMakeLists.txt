cmake_minimum_required(VERSION 3.16)

# Resolve IDF_TARGET from the saved sdkconfig before choosing board defaults.
# Plain idf.py commands do not pass IDF_TARGET explicitly.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(IDF_TARGET STREQUAL "esp32p4")
    set(SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.common;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.esp32p4")
    set(p4_board_manager_defaults
        "${CMAKE_CURRENT_LIST_DIR}/components/gen_bmgr_codes/board_manager.defaults")
    if(EXISTS "${p4_board_manager_defaults}")
        list(APPEND SDKCONFIG_DEFAULTS "${p4_board_manager_defaults}")
    endif()
endif()

project(crystal_os VERSION 0.15.0)

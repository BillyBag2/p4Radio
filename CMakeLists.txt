cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/boards.cmake)
option(P4RADIO_HEADLESS "Build without the local display and touchscreen UI" ${BOARD_HEADLESS_DEFAULT})
if(P4RADIO_HEADLESS)
    if(NOT BOARD_HEADLESS_DEFAULT)
        message(FATAL_ERROR "P4RADIO_HEADLESS is supported by the JC-ESP32P4-M3-DEV board.")
    endif()
    set(ENV{P4RADIO_HEADLESS} ON)
else()
    set(ENV{P4RADIO_HEADLESS} OFF)
endif()
# Resolve board paths before IDF reads sdkconfig and selects the chip target.
# Board selection never changes IDF_TARGET.
get_filename_component(board_build_dir "${CMAKE_SOURCE_DIR}/build/${MY_BOARD}" ABSOLUTE)
if(NOT CMAKE_BINARY_DIR STREQUAL board_build_dir)
    message(FATAL_ERROR "Board '${MY_BOARD}' requires build directory '${board_build_dir}'. Select its project configuration.")
endif()
set(board_sdkconfig "${board_build_dir}/sdkconfig")
if(DEFINED SDKCONFIG)
    get_filename_component(requested_sdkconfig "${SDKCONFIG}" ABSOLUTE)
    if(NOT requested_sdkconfig STREQUAL board_sdkconfig)
        message(FATAL_ERROR "Board '${MY_BOARD}' requires SDKCONFIG='${board_sdkconfig}'.")
    endif()
endif()
# Normalize a relative preset path before component configuration resolves it
# again from the build directory.
set(SDKCONFIG "${board_sdkconfig}" CACHE FILEPATH "Board SDK configuration" FORCE)
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.${MY_BOARD}" CACHE STRING "Board SDK defaults")
if(NOT P4RADIO_HEADLESS)
    list(PREPEND SDKCONFIG_DEFAULTS "sdkconfig.display.defaults")
endif()
if(DEFINED P4RADIO_CONFIGURED_BOARD AND NOT P4RADIO_CONFIGURED_BOARD STREQUAL MY_BOARD)
    message(FATAL_ERROR "This CMake cache belongs to '${P4RADIO_CONFIGURED_BOARD}', not '${MY_BOARD}'. Use a separate build directory.")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Keep the existing Waveshare 86 lock and pin other boards independently:
# their BSP dependency trees and chip targets can differ.
if(MY_BOARD STREQUAL "waveshare86")
    set(DEPENDENCIES_LOCK "${CMAKE_SOURCE_DIR}/dependencies.lock")
elseif(BOARD_HEADLESS_DEFAULT AND NOT P4RADIO_HEADLESS)
    set(DEPENDENCIES_LOCK "${CMAKE_SOURCE_DIR}/dependencies.${MY_BOARD}.display.lock")
else()
    set(DEPENDENCIES_LOCK "${CMAKE_SOURCE_DIR}/dependencies.${MY_BOARD}.lock")
endif()
idf_build_set_property(DEPENDENCIES_LOCK "${DEPENDENCIES_LOCK}")
if(P4RADIO_HEADLESS)
    idf_build_set_property(MINIMAL_BUILD ON)
endif()
p4radio_validate_board_target()
set(P4RADIO_CONFIGURED_BOARD "${MY_BOARD}" CACHE INTERNAL "Board owning this build directory")
project(p4Radio)

# IDF owns a shared managed_components directory and replaces board-specific
# dependencies on reconfiguration. Make the other cached build reconfigure on
# its next use too, while preserving both boards' compiled output and sdkconfig.
set(active_board_file "${CMAKE_SOURCE_DIR}/build/active-board")
file(GENERATE OUTPUT "${active_board_file}" CONTENT "${MY_BOARD}\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${active_board_file}")

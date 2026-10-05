cmake_minimum_required(VERSION 3.16)
project(skinsim CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point these at your checkouts. LVGL defaults to a clone beside this file (see README.md);
# the other two are downloaded by firmware/fetch_repos.py.
set(LVGL_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/lvgl"                       CACHE PATH "LVGL v9.4.0")
set(SUIT_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/smooth_ui_toolkit" CACHE PATH "smooth_ui_toolkit")
set(MCLOG_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/mooncake_log"      CACHE PATH "mooncake_log")
set(FW_MAIN    "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/main")

# --- LVGL -------------------------------------------------------------------
# lv_conf.h lives next to this file.
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
add_subdirectory(${LVGL_DIR} ${CMAKE_BINARY_DIR}/lvgl EXCLUDE_FROM_ALL)
target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- mooncake_log -----------------------------------------------------------
file(GLOB_RECURSE MCLOG_SRCS ${MCLOG_DIR}/src/*.cpp ${MCLOG_DIR}/src/*.cc)
add_library(mclog STATIC ${MCLOG_SRCS})
target_include_directories(mclog PUBLIC ${MCLOG_DIR}/src)

# --- smooth_ui_toolkit ------------------------------------------------------
# Built directly rather than via its own CMakeLists, which pulls in raylib examples
# and tests we don't need here.
file(GLOB_RECURSE SUIT_SRCS ${SUIT_DIR}/src/*.c ${SUIT_DIR}/src/*.cc ${SUIT_DIR}/src/*.cpp)
add_library(suit STATIC ${SUIT_SRCS})
target_include_directories(suit PUBLIC ${SUIT_DIR}/src ${SUIT_DIR}/src/lvgl)
target_compile_definitions(suit PUBLIC SMOOTH_UI_TOOLKIT_ENABLE_DEFAULT_HAL=1)
target_link_libraries(suit PUBLIC lvgl mclog)

# --- the real skin sources --------------------------------------------------
set(SKIN_SRCS
    ${FW_MAIN}/stackchan/avatar/skins/skins.cpp
    ${FW_MAIN}/stackchan/avatar/skins/default/default.cpp
    ${FW_MAIN}/stackchan/avatar/skins/default/eyes.cpp
    ${FW_MAIN}/stackchan/avatar/skins/default/mouth.cpp
    ${FW_MAIN}/stackchan/avatar/skins/default/speech_bubble.cpp
    ${FW_MAIN}/stackchan/avatar/skins/volt/volt.cpp
    ${FW_MAIN}/stackchan/avatar/skins/volt/eyes.cpp
    ${FW_MAIN}/stackchan/avatar/skins/volt/mouth.cpp
    ${FW_MAIN}/stackchan/avatar/skins/max/max.cpp
    ${FW_MAIN}/stackchan/avatar/skins/default/assets/default_bubble_arrow.c
)

add_executable(skinsim
    main.cpp
    stubs.cpp
    ${SKIN_SRCS}
)

target_include_directories(skinsim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FW_MAIN}
)

target_link_libraries(skinsim PRIVATE lvgl suit mclog)

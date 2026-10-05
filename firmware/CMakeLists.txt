# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# StackyChan fork version.
#
# Deliberately higher than any M5Stack release. This is a second, independent guard
# against the OTA server replacing this firmware: even if the compile-time disable
# (CONFIG_STACKYCHAN_DISABLE_AUTO_OTA) were ever turned off, the server should not
# consider a 1.4.x stock image to be "newer" than this.
#
# It also makes the boot log unambiguous about which firmware is actually running —
# which cost us a full debugging round when a stock image silently took over ota_1.
set(PROJECT_VER "9.9.3-stackychan")
add_definitions(-DFIRMWARE_VERSION=\"${PROJECT_VER}\")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

# Auto-load a local sdkconfig overlay (git-ignored) when present. Lets
# self-hosted / custom deployments pin CONFIG_STACKCHAN_SERVER_URL,
# CONFIG_OTA_URL, etc. without touching committed defaults.
# Manual equivalent: SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.local" idf.py build
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.local")
    set(SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.local"
        CACHE STRING "ESP-IDF sdkconfig default files (auto-extended with local overlay)" FORCE)
    message(STATUS "StackChan: detected sdkconfig.defaults.local, applying overlay")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stack-chan)

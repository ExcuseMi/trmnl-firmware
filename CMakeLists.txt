cmake_minimum_required(VERSION 3.16.0)
# sdkconfig.defaults lives under sdkconfigs/ (per-target configs are redirected there
# via board_build.esp-idf.sdkconfig_path in platformio.ini). Must be set before project().
# Only set the default when an env hasn't supplied its own SDKCONFIG_DEFAULTS chain
# (via board_build.cmake_extra_args), so board-specific overrides can layer on top.
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "${CMAKE_SOURCE_DIR}/sdkconfigs/sdkconfig.defaults")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(DEPENDENCIES_LOCK "dependencies.lock.${IDF_TARGET}")
# TinyUSB (espressif/tinyusb, the X's USB drive) reads its tusb_config.h from the project
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_SOURCE_DIR}/src/tusb" APPEND)
project(trmnl-firmware)

# ESP-IDF's USB hub driver (ext_hub.c) can get a second release action for a hub it released already; its
# device_release() then asserts waiting_release (IllegalInstruction at ext_hub.c:509: fw-usb25 on "back to serial",
# fw-latest29 when the charger went unreadable mid-drawing with a tablet behind a hub). The second release is a no-op
# (the hub is RELEASED, waiting_free set), so it is ignored and counted: trmnl_ext_hub_second_releases, which
# src/usb_keyboard.cpp logs and links against (the patch missing: the X build does not link).
# The patched copy is generated from the framework's own file; the build stops if that line is not there.
if(TARGET __idf_usb)
    set(_ext_hub "$ENV{IDF_PATH}/components/usb/ext_hub.c")
    get_target_property(_usb_srcs __idf_usb SOURCES)
    list(FIND _usb_srcs "${_ext_hub}" _ext_hub_i)
    if(_ext_hub_i GREATER -1)
        file(READ "${_ext_hub}" _ext_hub_text)
        set(_ext_hub_old "static void device_release(ext_hub_dev_t *ext_hub_dev)\n{\n    ESP_LOGD(EXT_HUB_TAG, \"[%d] Device release\", ext_hub_dev->constant.dev_addr);\n\n    EXT_HUB_ENTER_CRITICAL();\n    assert(ext_hub_dev->dynamic.flags.waiting_release); // Sanity check\n")
        set(_ext_hub_new "uint32_t trmnl_ext_hub_second_releases; // trmnl: counted, src/usb_keyboard.cpp logs it\n\nstatic void device_release(ext_hub_dev_t *ext_hub_dev)\n{\n    ESP_LOGD(EXT_HUB_TAG, \"[%d] Device release\", ext_hub_dev->constant.dev_addr);\n\n    EXT_HUB_ENTER_CRITICAL();\n    if (!ext_hub_dev->dynamic.flags.waiting_release) { // trmnl: released already, a second release action\n        trmnl_ext_hub_second_releases++;\n        EXT_HUB_EXIT_CRITICAL();\n        return;\n    }\n")
        string(FIND "${_ext_hub_text}" "${_ext_hub_old}" _ext_hub_at)
        if(_ext_hub_at EQUAL -1)
            message(FATAL_ERROR "ext_hub.c: device_release's waiting_release assert not found (ESP-IDF changed): check the double release by hand")
        endif()
        string(REPLACE "${_ext_hub_old}" "${_ext_hub_new}" _ext_hub_text "${_ext_hub_text}")
        set(_ext_hub_out "${CMAKE_BINARY_DIR}/trmnl_patched/ext_hub.c")
        set(_ext_hub_was "")
        if(EXISTS "${_ext_hub_out}")
            file(READ "${_ext_hub_out}" _ext_hub_was)
        endif()
        if(NOT _ext_hub_was STREQUAL _ext_hub_text) # rewritten only when it changed: no rebuild per configure
            file(WRITE "${_ext_hub_out}" "${_ext_hub_text}")
        endif()
        list(REMOVE_AT _usb_srcs ${_ext_hub_i})
        list(APPEND _usb_srcs "${_ext_hub_out}")
        set_property(TARGET __idf_usb PROPERTY SOURCES ${_usb_srcs})
        target_include_directories(__idf_usb PRIVATE "$ENV{IDF_PATH}/components/usb")
        message(STATUS "trmnl: ext_hub.c with the second release ignored")
    endif()
endif()

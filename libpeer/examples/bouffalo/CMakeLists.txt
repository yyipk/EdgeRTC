cmake_minimum_required(VERSION 3.15)

get_filename_component(EDGERTC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})
add_subdirectory("${EDGERTC_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/EdgeRTC")

sdk_add_compile_definitions(-DCONFIG_CLI_CMD_ENABLE)
sdk_add_compile_definitions(-DCONFIG_USING_TLS)
sdk_add_compile_definitions(-DHAVE_CONFIG_H)
sdk_add_compile_definitions(-DCONFIG_RECORD_TASK_STACK_SIZE=6144)
sdk_add_compile_definitions(-DCONFIG_XCODEC_INPUT_TASK_STACK_SIZE=1024)
sdk_add_compile_definitions(-DCONFIG_IMAGE_SENSOR_GC0308_ONLY=1)
sdk_add_compile_definitions(-DAGENT_MAX_CANDIDATES=10)
sdk_add_compile_definitions(-DAGENT_MAX_CANDIDATE_PAIRS=30)
sdk_add_compile_definitions(-DCONFIG_SDP_BUFFER_SIZE=4096)
sdk_add_compile_definitions(-DCONFIG_DTLS_USE_ECDSA=1)
sdk_add_compile_definitions(-DCONFIG_EDGERTC_SCTP_DEBUG=0)
sdk_add_compile_definitions(-DCONFIG_EDGERTC_SIGNALING_DEBUG=1)

if(CHIP STREQUAL "bl618dg")
    sdk_add_compile_definitions(-DLOG_REDIRECT=1)
endif()

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app/board_gpio_overlay.c
    ${CMAKE_CURRENT_SOURCE_DIR}/app/lwip_chksum_copy.c
)

if(CONFIG_BSP_LCD)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app/app_ui.c)
endif()

if(NOT CHIP STREQUAL "bl618dg")
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app/multimedia_port.c)
endif()
if(CHIP STREQUAL "bl618dg")
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app/bl618dg_stubs.c
        ${BL_SDK_BASE}/examples/audio/common/multimedia_port_common.c)
    sdk_add_compile_definitions(-DCONFIG_AUDIO_PA_PIN=19)
endif()

sdk_add_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/app)
sdk_add_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/config)

sdk_set_main_file(${CMAKE_CURRENT_SOURCE_DIR}/app/main.c)

project(webrtc)

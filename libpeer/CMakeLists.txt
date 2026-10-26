cmake_minimum_required(VERSION 3.16)
cmake_policy(VERSION 3.16)

if(CONFIG_LIBPEER_USE_EXTERNAL_TRANSPORT)
  set(PEER_SIGNALING_BACKEND_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/peer_http_external.c
    ${CMAKE_CURRENT_LIST_DIR}/src/peer_mqtt_external.c
  )
  set(PEER_SIGNALING_PROTOCOL_SOURCES)
else()
  include(${CMAKE_CURRENT_LIST_DIR}/../coreHTTP/httpFilePaths.cmake)
  include(${CMAKE_CURRENT_LIST_DIR}/../coreMQTT/mqttFilePaths.cmake)
  set(PEER_SIGNALING_BACKEND_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/peer_http_core.c
    ${CMAKE_CURRENT_LIST_DIR}/src/peer_mqtt_core.c
  )
  set(PEER_SIGNALING_PROTOCOL_SOURCES
    ${HTTP_SOURCES}
    ${MQTT_SOURCES}
    ${MQTT_SERIALIZER_SOURCES}
  )
endif()

if(COMMAND sdk_generate_library)
  sdk_generate_library()

  sdk_library_add_sources(
    net_buf/buf.c
    net_buf/buf_simple.c
    net_buf/net_buf_port_freertos.c
    src/address.c
    src/agent.c
    src/base64.c
    src/dtls_srtp.c
    src/ice.c
    src/mdns.c
    src/nat_pmp.c
    src/peer.c
    src/peer_connection.c
    src/peer_signaling.c
    src/ports.c
    src/rtcp.c
    src/rtp.c
    src/sctp.c
    src/sctp_tx.c
    src/sctp_rx.c
    src/sdp.c
    src/socket.c
    src/ssl_transport.c
    src/stun.c
    src/utils.c
    ${PEER_SIGNALING_BACKEND_SOURCES}
    ${PEER_SIGNALING_PROTOCOL_SOURCES}
  )

  sdk_add_include_directories(src)
  sdk_add_include_directories(net_buf/include)

  add_subdirectory(src/jitter_buffer)
  if(NOT CONFIG_LIBPEER_USE_EXTERNAL_TRANSPORT)
    sdk_add_include_directories(${HTTP_INCLUDE_PUBLIC_DIRS})
    sdk_add_include_directories(${MQTT_INCLUDE_PUBLIC_DIRS})
  endif()

  set_source_files_properties(
    src/base64.c
    src/peer_signaling.c
    PROPERTIES COMPILE_DEFINITIONS
    "base64_encode=peer_base64_encode;base64_decode=peer_base64_decode"
  )

  sdk_add_compile_definitions(-D__BYTE_ORDER=__LITTLE_ENDIAN)
  sdk_add_compile_definitions(-DCONFIG_USE_LWIP=1)
  sdk_add_compile_definitions(-DCONFIG_USE_USRSCTP=0)
  sdk_add_compile_definitions(-DCONFIG_MBEDTLS_2_X=1)
  sdk_add_compile_definitions(-DCONFIG_AUDIO_BUFFER_SIZE=8096)
  sdk_add_compile_definitions(-DCONFIG_DATA_BUFFER_SIZE=80960)
  if(CONFIG_LIBPEER_USE_EXTERNAL_TRANSPORT)
    sdk_add_compile_definitions(-DMQTT_USE_MBEDTLS=1)
  else()
    sdk_add_compile_definitions(-DHTTP_DO_NOT_USE_CUSTOM_CONFIG=1)
    sdk_add_compile_definitions(-DMQTT_DO_NOT_USE_CUSTOM_CONFIG=1)
  endif()
  sdk_add_subdirectory_ifdef(CONFIG_EDGERTC_PLATFORM_BOUFFALO platform/bouffalo)
  return()
endif()

if (DEFINED ENV{IDF_PATH})
  file(GLOB ESP32_CODES "./src/*.c")
  list(FILTER ESP32_CODES EXCLUDE REGEX "/peer_(http|mqtt)_(core|external)\\.c$")
  idf_component_register(
   SRCS ${ESP32_CODES} ${PEER_SIGNALING_BACKEND_SOURCES} ${PEER_SIGNALING_PROTOCOL_SOURCES}
   INCLUDE_DIRS "./include" ${HTTP_INCLUDE_PUBLIC_DIRS} ${MQTT_INCLUDE_PUBLIC_DIRS}
   PRIV_INCLUDE_DIRS "./src"
   REQUIRES mbedtls srtp json esp_netif
  )
  add_definitions("-DCONFIG_USE_LWIP=1" "-DCONFIG_USE_USRSCTP=0" "-DCONFIG_AUDIO_BUFFER_SIZE=8096" "-DCONFIG_DATA_BUFFER_SIZE=102400" "-D__BYTE_ORDER=__LITTLE_ENDIAN")
  if(NOT CONFIG_LIBPEER_USE_EXTERNAL_TRANSPORT)
    add_definitions("-DHTTP_DO_NOT_USE_CUSTOM_CONFIG" "-DMQTT_DO_NOT_USE_CUSTOM_CONFIG")
  endif()
  return()
endif()

project(peer)

option(ENABLE_TESTS "Enable tests" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(ADDRESS_SANITIZER "Build with AddressSanitizer." OFF)
option(MEMORY_SANITIZER "Build with MemorySanitizer." OFF)
option(THREAD_SANITIZER "Build with ThreadSanitizer." OFF)
option(UNDEFINED_BEHAVIOR_SANITIZER "Build with UndefinedBehaviorSanitizer." OFF)

include(ExternalProject)

set(PEER_MBEDTLS_USER_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/examples/generic/mbedtls_user_config.h)
add_compile_definitions(MBEDTLS_USER_CONFIG_FILE="${PEER_MBEDTLS_USER_CONFIG}")

include_directories(${CMAKE_BINARY_DIR}/dist/include ${CMAKE_BINARY_DIR}/dist/include/cjson)

link_directories(${CMAKE_BINARY_DIR}/dist/lib)

set(DEP_LIBS "srtp2" "usrsctp" "mbedtls" "mbedcrypto" "mbedx509" "cjson")
# Extended debug information (symbols, source code, and macro definitions)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g3")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3")

function(enableSanitizer SANITIZER)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g -fsanitize=${SANITIZER} -fno-omit-frame-pointer -fno-optimize-sibling-calls" PARENT_SCOPE)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=${SANITIZER}" PARENT_SCOPE)
endfunction()

if(ADDRESS_SANITIZER)
  enableSanitizer("address")
endif()

if(MEMORY_SANITIZER)
  enableSanitizer("memory")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize-memory-track-origins")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize-memory-track-origins")
endif()

if(THREAD_SANITIZER)
  enableSanitizer("thread")
endif()

if(UNDEFINED_BEHAVIOR_SANITIZER)
  enableSanitizer("undefined")
endif()

add_definitions("-Wunused-variable -Werror=sequence-point -Werror=pointer-sign -Werror=return-type -Werror=sizeof-pointer-memaccess -Wincompatible-pointer-types -DHTTP_DO_NOT_USE_CUSTOM_CONFIG -DMQTT_DO_NOT_USE_CUSTOM_CONFIG")

add_subdirectory(src)
add_subdirectory(examples)

if(ENABLE_TESTS)
  add_subdirectory(tests)
endif()

ExternalProject_Add(cjson
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cJSON
  CMAKE_ARGS
    -DCMAKE_C_FLAGS="-fPIC"
    -DBUILD_SHARED_LIBS=off
    -DENABLE_CJSON_TEST=off
    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/dist
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
)

ExternalProject_Add(mbedtls
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../mbedtls
  CMAKE_ARGS
    "-DCMAKE_C_FLAGS=-fPIC -DMBEDTLS_USER_CONFIG_FILE=\\\"${PEER_MBEDTLS_USER_CONFIG}\\\""
    -DENABLE_TESTING=off
    -DENABLE_PROGRAMS=off
    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/dist
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
)

ExternalProject_Add(srtp2
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../libsrtp
  CMAKE_ARGS
    -DCMAKE_C_FLAGS="-fPIC"
    -DTEST_APPS=off
    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/dist
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
)

ExternalProject_Add(usrsctp
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../usrsctp
  CMAKE_ARGS
    -DCMAKE_C_FLAGS="-fPIC"
    -Dsctp_build_programs=off
    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/dist
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
)

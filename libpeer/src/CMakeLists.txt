project(lib)

file(GLOB SRCS "*.c")
list(FILTER SRCS EXCLUDE REGEX "/peer_(http|mqtt)_(core|external)\\.c$")
list(APPEND SRCS ${PEER_SIGNALING_BACKEND_SOURCES})

file(GLOB HEADERS "peer.h" "peer_connection.h" "peer_signaling.h")

add_library(peer
  ${SRCS}
  ${PEER_SIGNALING_PROTOCOL_SOURCES}
)

include_directories(peer PUBLIC
  ${HTTP_INCLUDE_PUBLIC_DIRS}
  ${MQTT_INCLUDE_PUBLIC_DIRS}
)

add_dependencies(peer cjson mbedtls usrsctp srtp2)

target_link_libraries(peer ${DEP_LIBS})

set_target_properties(peer PROPERTIES PUBLIC_HEADER "${HEADERS}")

install(TARGETS peer
  LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_PREFIX}/include/
)

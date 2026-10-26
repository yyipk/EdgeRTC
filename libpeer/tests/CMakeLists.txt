project(tests)

file(GLOB SRCS "*.c")

include_directories(${PROJECT_SOURCE_DIR}/../src)

foreach(sourcefile ${SRCS})
  string(REPLACE ".c" "" appname ${sourcefile})
  string(REPLACE "${PROJECT_SOURCE_DIR}/" "" appname ${appname})
  add_executable(${appname} ${sourcefile})
  if(appname STREQUAL "test_sctp_retransmit")
    target_compile_definitions(${appname} PRIVATE CONFIG_USE_USRSCTP=0)
    target_include_directories(${appname} PRIVATE include ../../stats/include)
  else()
    target_link_libraries(${appname} peer pthread)
  endif()
endforeach(sourcefile ${TEST_SRCS})

target_link_libraries(test_peer_connection cjson)

cmake_minimum_required(VERSION 3.15)

if(COMMAND sdk_generate_library)
    sdk_generate_library(edgertc_jitter_buffer)
    sdk_library_add_sources(
        src/edge_audio_jitter_buffer.c
        src/rtp_jitter.c
    )
    sdk_add_include_directories(include)
    return()
endif()

project(edgertc_jitter_buffer C)

add_library(edgertc_jitter_buffer STATIC
    src/edge_audio_jitter_buffer.c
    src/rtp_jitter.c
)
target_include_directories(edgertc_jitter_buffer PUBLIC include)

include(CTest)
if(BUILD_TESTING)
    add_executable(edgertc_audio_jitter_test
        tests/test_edge_audio_jitter_buffer.c
    )
    target_link_libraries(edgertc_audio_jitter_test PRIVATE
        edgertc_jitter_buffer
    )
    add_test(NAME edgertc_audio_jitter_test
             COMMAND edgertc_audio_jitter_test)

    add_executable(edgertc_rtp_jitter_test
        tests/test_rtp_jitter.c
    )
    target_link_libraries(edgertc_rtp_jitter_test PRIVATE
        edgertc_jitter_buffer
    )
    add_test(NAME edgertc_rtp_jitter_test
             COMMAND edgertc_rtp_jitter_test)
endif()

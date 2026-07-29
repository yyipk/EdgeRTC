cmake_minimum_required(VERSION 3.15)

if(COMMAND sdk_generate_library)
    sdk_generate_library(edgertc_audio_jitter)
    sdk_library_add_sources(src/edge_audio_jitter_buffer.c)
    sdk_add_include_directories(include)
    return()
endif()

project(edgertc_audio_jitter C)

add_library(edgertc_audio_jitter STATIC
    src/edge_audio_jitter_buffer.c
)
target_include_directories(edgertc_audio_jitter PUBLIC include)

include(CTest)
if(BUILD_TESTING)
    add_executable(edgertc_audio_jitter_test
        tests/test_edge_audio_jitter_buffer.c
    )
    target_link_libraries(edgertc_audio_jitter_test PRIVATE
        edgertc_audio_jitter
    )
    add_test(NAME edgertc_audio_jitter_test
             COMMAND edgertc_audio_jitter_test)
endif()

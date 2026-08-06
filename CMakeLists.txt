set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_AR "${CMAKE_C_COMPILER_AR}")

add_library(edgertc_compile_options INTERFACE)
target_compile_options(edgertc_compile_options INTERFACE -O2)

sdk_add_subdirectory_ifdef(CONFIG_LIBPEER libpeer)
sdk_add_subdirectory_ifdef(CONFIG_LIBPEER alg/audio/jitter_buffer)
sdk_add_subdirectory_ifdef(CONFIG_AEC4 alg/audio/aec4)

foreach(target liblibpeer libedgertc_audio_jitter libaec4)
    if(TARGET ${target})
        target_link_libraries(${target} PRIVATE edgertc_compile_options)
    endif()
endforeach()

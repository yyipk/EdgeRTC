set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_AR "${CMAKE_C_COMPILER_AR}")

add_library(edgertc_compile_options INTERFACE)
target_compile_options(edgertc_compile_options INTERFACE -O2)

if(CONFIG_LIBPEER)
    if(CONFIG_LIBPEER_USE_EXTERNAL_TRANSPORT)
        set(CONFIG_MQTT y PARENT_SCOPE)
        set(CONFIG_HTTP y PARENT_SCOPE)
        set(CONFIG_HTTP_CLIENT y PARENT_SCOPE)
    endif()
endif()

sdk_add_subdirectory_ifdef(CONFIG_LIBPEER libpeer)
sdk_add_subdirectory_ifdef(CONFIG_LIBPEER alg/jitter_buffer)
sdk_add_subdirectory_ifdef(CONFIG_AEC4 alg/audio/aec4)

foreach(target liblibpeer libedgertc_jitter_buffer libaec4)
    if(TARGET ${target})
        target_link_libraries(${target} PRIVATE edgertc_compile_options)
    endif()
endforeach()

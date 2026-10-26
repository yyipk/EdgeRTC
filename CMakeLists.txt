set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_AR "${CMAKE_C_COMPILER_AR}")

sdk_add_include_directories(stats/include)
if(CONFIG_EDGERTC_STATS)
    sdk_generate_library(edgertc)
    sdk_library_add_sources(stats/edgertc_stats.c)
endif()

add_library(edgertc_compile_options INTERFACE)
target_compile_options(edgertc_compile_options INTERFACE -O2)

if(CONFIG_LIBPEER OR CONFIG_EDGERTC_PLATFORM_BOUFFALO)
    add_subdirectory(libpeer)
endif()

foreach(target liblibpeer libedgertc_jitter_buffer libedgertc_bouffalo)
    if(TARGET ${target})
        target_link_libraries(${target} PRIVATE edgertc_compile_options)
    endif()
endforeach()

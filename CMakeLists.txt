set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_AR "${CMAKE_C_COMPILER_AR}")

sdk_add_subdirectory_ifdef(CONFIG_LIBPEER libpeer)
sdk_add_subdirectory_ifdef(CONFIG_LIBPEER alg/audio/jitter_buffer)
sdk_add_subdirectory_ifdef(CONFIG_AEC4 alg/audio/aec4)

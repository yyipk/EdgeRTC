sdk_generate_library(rnnoise)

sdk_add_include_directories(include)
sdk_add_private_include_directories(src)

sdk_add_private_compile_options(-UHAVE_CONFIG_H)
sdk_add_private_compile_definitions(-DFLOATING_POINT=1)

sdk_library_add_sources(
    src/celt_lpc.c
    src/denoise.c
    src/kiss_fft.c
    src/pitch.c
    src/rnn.c
    src/rnn_data.c
    src/rnnoise_static_tables.c
)

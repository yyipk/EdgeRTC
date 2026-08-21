sdk_generate_library(speexdsp)

sdk_add_include_directories(include)

sdk_add_private_compile_options(-UHAVE_CONFIG_H)
sdk_add_private_compile_definitions(
    -DDISABLE_FLOAT_API
    -DEXPORT=
    -DFIXED_POINT
    -DUSE_KISS_FFT
)

sdk_library_add_sources(
    libspeexdsp/mdf.c
    libspeexdsp/fftwrap.c
    libspeexdsp/kiss_fft.c
    libspeexdsp/kiss_fftr.c
)

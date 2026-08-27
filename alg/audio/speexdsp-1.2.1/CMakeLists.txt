sdk_generate_library(speexdsp)

sdk_add_include_directories(include)

sdk_add_private_compile_options(-UHAVE_CONFIG_H)
sdk_add_private_compile_definitions(
    -DEXPORT=
    -DFIXED_POINT=1
    -DUSE_KISS_FFT
)

if(CONFIG_SPEEXDSP_ALG)
    sdk_library_add_sources(
        libspeexdsp/mdf.c
        libspeexdsp/fftwrap.c
        libspeexdsp/kiss_fft.c
        libspeexdsp/kiss_fftr.c
    )
endif()

if(CONFIG_MG_SPEEXDSP_AEC_PREPROCESS)
    sdk_library_add_sources(
        libspeexdsp/preprocess.c
        libspeexdsp/filterbank.c
    )
endif()

if(CONFIG_RNNOISE_NS)
    sdk_library_add_sources(libspeexdsp/resample.c)
endif()

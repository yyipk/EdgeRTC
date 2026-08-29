sdk_generate_library(aligenie_ns)

sdk_add_include_directories(.)
sdk_add_private_include_directories(${EDGERTC_ACCEL_DIR})
sdk_add_private_compile_options(-include${CMAKE_CURRENT_LIST_DIR}/ns_rv32_abi.h)

sdk_library_add_sources(
    NS_recovered.c
    NS3_recovered.c
    noise_suppression_x_recovered.c
    nsx_core_recovered.c
    add_sat_w16_recovered.c
    complex_bit_reverse_recovered.c
    complex_fft_recovered.c
    complex_ifft_recovered.c
    division_operations_recovered.c
    energy_recovered.c
    get_scaling_square_recovered.c
    get_size_in_bits_recovered.c
    math_approx_recovered.c
    min_max_operations_recovered.c
    norm_u32_recovered.c
    norm_w16_recovered.c
    norm_w32_recovered.c
    spl_sqrt_recovered.c
    tdereverb_x_recovered.c
    tspl_sin_table_1024_recovered.c
)

if(CMAKE_C_COMPILER MATCHES "riscv")
    sdk_library_add_sources(${EDGERTC_ACCEL_DIR}/e907/dsp_fft.c)
else()
    sdk_library_add_sources(${EDGERTC_ACCEL_DIR}/generic/dsp_fft.c)
endif()

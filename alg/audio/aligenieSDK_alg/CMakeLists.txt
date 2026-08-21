sdk_generate_library(aligenie_aec)

sdk_add_include_directories(.)

sdk_library_add_sources(
    HPF_recovered.c
    PPFBankAnalyzer_recovered.c
    PPFBankSynthesizer_recovered.c
    pre_emp_recovered.c
    de_emp_recovered.c
    add_win_mac_rev_recovered.c
    add_win_mul_rev_recovered.c
    rfft640_f32_recovered.c
    tFeOpt_PFBA_Win320_1920_Delay960_recovered.c
    aec_recovered.c
    DRAEC_recovered.c
    draec_filter_recovered.c
    draec_add_noise_recovered.c
    draec_calc_sqrt_recovered.c
    draec_update_path_recovered.c
    dr_va_init_recovered.c
    dr_giv_rot_recovered.c
    aec_recovered_util.c
    aec_pcm.c
)

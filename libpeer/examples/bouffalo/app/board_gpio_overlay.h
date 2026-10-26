#ifndef BOARD_GPIO_OVERLAY_H
#define BOARD_GPIO_OVERLAY_H

#ifdef BL618DG
void board_ap_console_gpio_init(void);
void board_np_console_gpio_init(void);
void board_lp_console_gpio_init(void);
void board_i2s_codec_gpio_init(void);
#endif

void board_dvp_gpio_init(void);
void board_lcd_dbi_type_b_gpio_init(void);
void board_audio_pll_config_for_rate(unsigned int samplerate_hz);

#endif

#ifndef LV_CONF_H
#define LV_CONF_H

#include "lcd.h"

#define LV_COLOR_DEPTH LCD_COLOR_DEPTH
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
/* All LVGL calls run in the display task. */
#define LV_USE_OS LV_OS_NONE
#define LV_USE_DRAW_SW 1
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0
#endif

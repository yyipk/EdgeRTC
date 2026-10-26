#include <stdarg.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "msp_port.h"
#include "utils.h"

void peer_log(char *level_tag, const char *file_name, int line_number,
              const char *fmt, ...)
{
    va_list args;

    /* SDK console output avoids the picolibc stdout stream placeholder. */
    printf("[%lu] %s %s:%d ", (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS),
           level_tag, file_name, line_number);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\r\n");
}

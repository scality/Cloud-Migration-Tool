#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cloudmig.h"

enum cloudmig_loglevel gl_loglevel = INFO_LVL;

void cloudmig_log(enum cloudmig_loglevel lvl, const char* format, ...)
{
    if (lvl >= gl_loglevel)
    {
        va_list args;
        char*   fmt = 0;
        // 20 for "cloudmig :" + log_level string length.
        if ((fmt = calloc(20 + strlen(format) + 1, sizeof(*fmt))) == 0)
        {
            PRINTERR("%s: could not allocate memory.\n", __FUNCTION__);
            return ;
        }
        strcpy(fmt, "cloudmig: ");
        switch (lvl)
        {
        case DEBUG_LVL:
            strcat(fmt, "[DEBUG]");
            break ;
        case INFO_LVL:
            strcat(fmt, "[INFO]");
            break ;
        case WARN_LVL:
            strcat(fmt, "[WARN]");
            break ;
        default:
            break ;
        }
        strcat(fmt, format);
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        free(fmt);
    }
}

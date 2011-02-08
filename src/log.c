// Copyright (c) 2011, David Pineau
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER AND CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

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

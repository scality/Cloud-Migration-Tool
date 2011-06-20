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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cloudmig.h"
#include "options.h"

enum cloudmig_loglevel gl_loglevel = INFO_LVL;
static FILE* logstream = NULL;

int cloudmig_openlog(char* filename)
{
    if (logstream && logstream != stderr)
        return EXIT_FAILURE;

    // If a filename is set, open it...
    if (filename)
        logstream = fopen(filename, "a+");
    else
        logstream = stderr;
    if (logstream == NULL)
    {
        fprintf(stderr,
                "cloudmig: [ERR] Could not open file %s for logging : %s\n",
                filename, strerror(errno));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

void cloudmig_closelog(void)
{
    if (logstream == NULL || logstream == stderr)
    {
        logstream = NULL;
        return ;
    }
    fclose(logstream);
    logstream = NULL;
}

void cloudmig_log(enum cloudmig_loglevel lvl, const char* format, ...)
{
    if (lvl >= gl_loglevel
        && !(gl_options->flags & BACKGROUND_MODE && logstream == stderr))
    {
        va_list args;
        char*   loglvl_str = 0;

        switch (lvl)
        {
        case DEBUG_LVL:
            loglvl_str = "DEBUG";
            break ;
        case INFO_LVL:
            loglvl_str = "INFO";
            break ;
        case WARN_LVL:
            loglvl_str = "WARN";
            break ;
        case STATUS_LVL:
            loglvl_str = "STATUS";
            break ;
        case ERR_LVL:
            loglvl_str = "ERR";
            break ;
        default:
            break ;
        }
        va_start(args, format);
        if (lvl == ERR_LVL && logstream == NULL)
        {
            fprintf(stderr, "cloudmig: [%s]", loglvl_str);
            vfprintf(stderr, format, args);
        }
        if (logstream)
        {
            fprintf(logstream, "cloudmig: [%s]", loglvl_str);
            vfprintf(logstream, format, args);
        }
        va_end(args);
    }
}

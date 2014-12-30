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

#ifndef __SD_CLOUMIG_OPT_H__
#define __SD_CLOUMIG_OPT_H__

enum cloudmig_flags
{
    SRC_PROFILE_NAME    = 1 << 0,
    DEST_PROFILE_NAME   = 1 << 1,
    IGNORE_STATUS       = 1 << 2,
    RESUME_MIGRATION    = 1 << 3,
    QUIET               = 1 << 5,
    DELETE_SOURCE_DATA  = 1 << 6,
    AUTO_CREATE_DIRS    = 1 << 7,
};

struct cloudmig_options
{
    int         is_src_name;
    int         is_dest_name;
    int         flags;
    int         trace_flags;
    int         nb_threads;
    const char  *src_profile;
    const char  *dest_profile;
    char        *logfile;
    char        **src_buckets;
    char        **dst_buckets;
    char        *config;
};

#define OPTIONS_INITIALIZER \
{                           \
    0,                      \
    0,                      \
    0,                      \
    0,                      \
    1,                      \
    NULL,                   \
    NULL,                   \
    NULL,                   \
    NULL,                   \
    NULL,                   \
    NULL                    \
}

// Used by config parser as well as command line arguments parser.
int opt_buckets(struct cloudmig_options *, char *arg);
int opt_trace(struct cloudmig_options *, char *arg);
int opt_verbose(char *arg);
int cloudmig_options_check(struct cloudmig_options *);

#endif /* ! __SD_CLOUMIG_OPT_H__ */

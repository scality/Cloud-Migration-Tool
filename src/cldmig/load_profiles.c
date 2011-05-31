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

#include <string.h>

#include <libgen.h>

#include "options.h"
#include "cloudmig.h"

int load_profiles(struct cloudmig_ctx* ctx)
{
    char* cpy = 0;
    char* profile_dir = 0;
    char* profile_name = 0;

    cloudmig_log(INFO_LVL, "[Loading Profiles]: Starting profiles loading...\n");

    /*
     * First, load the source profile...
     */
    cpy = strdup(gl_options->src_profile);
    if (gl_options->flags & SRC_PROFILE_NAME)
        profile_name = cpy;
    else
    {
        profile_name = basename(cpy);
        char * point = strrchr(profile_name, '.');
        if (point)
            *point = '\0';
        profile_dir = dirname(cpy);
    }
    if ((ctx->src_ctx = dpl_ctx_new(profile_dir, profile_name)) == NULL)
    {
        PRINTERR("Could not load source profile : %s/%s\n",
                 profile_dir, profile_name, strerror(errno));
        return (EXIT_FAILURE);
    }
    free(cpy);


    cpy = strdup(gl_options->dest_profile);
    profile_dir = 0;
    if (gl_options->flags & DEST_PROFILE_NAME)
        profile_name = cpy;
    else
    {
        profile_name = basename(cpy);
        char * point = strrchr(profile_name, '.');
        if (point)
            *point = '\0';
        profile_dir = dirname(cpy);
    }
    if ((ctx->dest_ctx = dpl_ctx_new(profile_dir, profile_name)) == NULL)
    {
        dpl_ctx_free(ctx->src_ctx);
        PRINTERR("Could not load destination profile : %s/%s\n",
                 profile_dir, profile_name);
        return (EXIT_FAILURE);
    }
    free(cpy);


    /*
     * If the debug option was given, let's activate every droplet traces.
     */
    if (gl_options->trace_flags != 0)
    {
        cloudmig_log(DEBUG_LVL,
                     "[Loading Profiles]: Activating droplet libary traces.\n");
        ctx->src_ctx->trace_level = gl_options->trace_flags;
        ctx->dest_ctx->trace_level = gl_options->trace_flags;
    }

    cloudmig_log(INFO_LVL,
    "[Loading Profiles]: Profiles loaded with success.\n");

    return (EXIT_SUCCESS);
}

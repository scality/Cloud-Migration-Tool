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
    int         ret;
    char        *profile_dir = 0;
    char        *profile_name = 0;
    dpl_ctx_t   *src = NULL;
    dpl_ctx_t   *dst = NULL;
    dpl_ctx_t   *status = NULL;
    char        *src_path = NULL;
    char        *dst_path = NULL;
    char        *status_path = NULL;

    cloudmig_log(INFO_LVL, "[Loading Profiles]: Starting...\n");

    profile_dir = 0;
    src_path = strdup(ctx->options.src_profile);
    if (src_path == NULL)
    {
        PRINTERR("[Loading profiles]: Could not allocate memory.");
        ret = EXIT_FAILURE;
        goto err;
    }
    if (ctx->options.flags & SRC_PROFILE_NAME)
        profile_name = src_path;
    else
    {
        profile_name = basename(src_path);
        char * dot = strrchr(profile_name, '.');
        if (dot)
            *dot = '\0';
        profile_dir = dirname(src_path);
    }
    src = dpl_ctx_new(profile_dir, profile_name);
    if (src == NULL)
    {
        PRINTERR("[Loading Profiles]: Could not load Source: %s/%s\n",
                 profile_dir, profile_name);
        ret = EXIT_FAILURE;
        goto err;
    }


    profile_dir = 0;
    dst_path = strdup(ctx->options.dest_profile);
    if (dst_path == NULL)
    {
        PRINTERR("[Loading profiles]: Could not allocate memory.");
        ret = EXIT_FAILURE;
        goto err;
    }
    if (ctx->options.flags & DEST_PROFILE_NAME)
        profile_name = dst_path;
    else
    {
        profile_name = basename(dst_path);
        char * dot = strrchr(profile_name, '.');
        if (dot)
            *dot = '\0';
        profile_dir = dirname(dst_path);
    }
    dst = dpl_ctx_new(profile_dir, profile_name);
    if (dst == NULL)
    {
        PRINTERR("[Loading Profiles]: Could not load Destination: %s/%s\n",
                 profile_dir, profile_name);
        ret = EXIT_FAILURE;
        goto err;
    }


    profile_dir = 0;
    status_path = strdup(ctx->options.status_profile);
    if (status_path == NULL)
    {
        PRINTERR("[Loading profiles]: Could not allocate memory.");
        ret = EXIT_FAILURE;
        goto err;
    }
    if (ctx->options.flags & STATUS_PROFILE_NAME)
        profile_name = status_path;
    else
    {
        profile_name = basename(status_path);
        char * dot = strrchr(profile_name, '.');
        if (dot)
            *dot = '\0';
        profile_dir = dirname(status_path);
    }
    status = dpl_ctx_new(profile_dir, profile_name);
    if (status == NULL)
    {
        PRINTERR("[Loading Profiles]: Could not load Status: %s/%s\n",
                 profile_dir, profile_name);
        ret = EXIT_FAILURE;
        goto err;
    }


    cloudmig_log(INFO_LVL, "[Loading Profiles]: Profiles loaded with success.\n");

    /* Validate by 'consuming' the droplet contexts*/
    ctx->src_ctx = src;
    ctx->dest_ctx = dst;
    ctx->status_ctx = status;
    src = dst = status = NULL;

    /*
     * If the debug option was given, let's activate every droplet traces.
     */
    if (ctx->options.trace_flags != 0)
    {
        cloudmig_log(DEBUG_LVL,
                     "[Loading Profiles]: Activating droplet libary traces.\n");
        ctx->src_ctx->trace_level = ctx->options.trace_flags;
        ctx->dest_ctx->trace_level = ctx->options.trace_flags;
        ctx->status_ctx->trace_level = ctx->options.trace_flags;
    }

    ret = EXIT_SUCCESS;

err:
    if (src_path)
        free(src_path);
    if (dst_path)
        free(dst_path);
    if (status_path)
        free(status_path);

    if (src)
        dpl_ctx_free(src);
    if (dst)
        dpl_ctx_free(dst);
    if (status)
        dpl_ctx_free(status);

    return ret;
}

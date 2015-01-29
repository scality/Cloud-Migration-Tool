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

#include <unistd.h>

#include <droplet.h>
#include <droplet/vfs.h>

#include "cloudmig.h"

char *cloudmig_urlencode(const char *str, size_t strlen)
{
    static char hex[] = "0123456789abcdef";
    char    *ret = NULL;
    char    *encstr = NULL;
    size_t  i;

    encstr = ret = malloc(strlen * 3 + 1);
    if (ret == NULL)
        return NULL;

    for (i=0; i < strlen; ++i)
    {
        if(isalnum(str[i]) || str[i] == '-' || str[i] == '_' || str[i] == '.' || str[i] == '~')
        {
            *encstr = str[i];
            encstr += 1;
        }
        else if (str[i] == ' ')
        {
            *encstr = '+';
            encstr += 1;
        }
        else
        {
            encstr[0] = '%';
            encstr[1] = hex[(str[i] >> 4) & 15];
            encstr[2] = hex[ str[i]       & 15];
            encstr += 3;
        }
    }
    *encstr = '\0';

    return ret;
}

void
delete_file(dpl_ctx_t *ctx, const char *what, char *path)
{
    dpl_status_t    dplret;

    cloudmig_log(DEBUG_LVL, "[Deleting %s File] Deleting file '%s'...\n",
                 what, path);

    dplret = dpl_unlink(ctx, path);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Deleting %s File] Could not delete the file %s : %s.\n",
                 what, path, dpl_status_str(dplret));
    }
}

void
delete_directory(dpl_ctx_t *ctx, const char *what, char *path)
{
    dpl_status_t dplret;

    cloudmig_log(DEBUG_LVL, "[Deleting %s Directory] Deleting '%s'...\n",
                 what, path);

    dplret = dpl_rmdir(ctx, path);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Deleting %s Directory] "
                 "Could not delete the directory %s : %s.\n",
                 what, path, dpl_status_str(dplret));
    }
}

void
delete_bucket(dpl_ctx_t *ctx, const char *what, char *path)
{
    dpl_status_t dplret;

    cloudmig_log(DEBUG_LVL, "[Deleting %s Bucket] Deleting '%s'...\n",
                 what, path);

    dplret = dpl_delete_bucket(ctx, path);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Deleting %s Bucket] "
                 "Could not delete the bucket %s : %s.\n",
                 what, path, dpl_status_str(dplret));
    }
}

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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cloudmig.h"

dpl_canned_acl_t
get_file_canned_acl(dpl_ctx_t* ctx, char *filename)
{
    dpl_sysmd_t		sysmd;
    dpl_status_t        dplret = DPL_SUCCESS;
    char                *bucket;
    dpl_canned_acl_t    acl = DPL_CANNED_ACL_UNDEF;

    cloudmig_log(DEBUG_LVL,
                 "[Migrating]: getting acl of file %s...\n",
                 filename);

    bucket = filename;
    filename = index(filename, ':');
    *filename = '\0';
    ++filename;

    dplret = dpl_get(ctx, bucket, filename, NULL/*option*/,
                     DPL_FTYPE_ANY,
                     NULL, NULL,
		     NULL/*DATA*/, NULL/*DATAsz*/, NULL /*MDp*/, &sysmd);
    if (dplret != DPL_SUCCESS)
    {
        cloudmig_log(INFO_LVL,
                     "[Migrating]: Could not retrieve acl of file %s : %s\n",
                     filename, dpl_status_str(dplret));
        goto err;
    }

    acl = sysmd.canned_acl;

err:
    --filename;
    *filename = ':';

    return acl;
}

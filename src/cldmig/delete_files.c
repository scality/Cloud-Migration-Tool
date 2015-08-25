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
#include <string.h>
#include <unistd.h>

#include <droplet.h>
#include <droplet/vfs.h>

#include "cloudmig.h"
#include "status_store.h"
#include "utils.h"

void delete_source(struct cloudmig_ctx *ctx)
{
    int                         found = 1;
    char                        *bucketpath = NULL;
    char                        *statuspath = NULL;
    struct file_transfer_state  filestate;

    cloudmig_log(INFO_LVL,
    "[Deleting Source]: Starting deletion of the migration's source...\n");

    status_store_reset_iteration(ctx);
    while (found == 1)
    {
        found = status_store_next_entry(ctx, &filestate);
        if (found < 0)
        {
            PRINTERR("[Deleting Source] Could not find next object entry to delete.\n");
            goto cleanup;
        }
        if (found == 1)
        {
            delete_file(ctx->src_ctx, "Source", filestate.src_path);
            status_store_release_entry(&filestate);
        }
    }

    status_store_delete(ctx);

    cloudmig_log(INFO_LVL,
    "[Deleting Source]: Deletion of the migration's source done.\n");

cleanup:
    if (bucketpath)
        free(bucketpath);
    if (statuspath)
        free(statuspath);
}

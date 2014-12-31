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

#include "cloudmig.h"

static void delete_file(struct cloudmig_ctx *ctx, char *bucket, char *filename)
{
    dpl_status_t dplret;

    cloudmig_log(DEBUG_LVL, "[Deleting Source]\t Deleting file '%s'...\n",
                 filename);

    dplret = dpl_delete(ctx->src_ctx, bucket, filename, NULL, DPL_FTYPE_REG, NULL);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not delete the file %s" " from the bucket %s : %s",
                 __FUNCTION__, filename, bucket, dpl_status_str(dplret));
    }
}


static void delete_status_bucket(struct cloudmig_ctx *ctx)
{
    dpl_status_t    dplret;
    dpl_vec_t       *objects = NULL;

    cloudmig_log(DEBUG_LVL, "[Deleting files]: Deleting status bucket...\n");

    dplret = dpl_list_bucket(ctx->src_ctx, ctx->status.bucket_name,
                             NULL, NULL, -1, &objects, NULL);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not list bucket %s for deletion : %s\n",
                 __FUNCTION__, ctx->status.bucket_name, dpl_status_str(dplret));
        goto deletebucket;
    }

    for (int i = 0; i < objects->n_items; ++i)
        delete_file(ctx, ctx->status.bucket_name,
		    ((dpl_object_t*)(objects->items[i]->ptr))->path);

deletebucket:
    dpl_delete_bucket(ctx->src_ctx, ctx->status.bucket_name);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not delete bucket %s : %s.\n",
                 __FUNCTION__, ctx->status.bucket_name, dpl_status_str(dplret));
        return ;
    }
    cloudmig_log(DEBUG_LVL, "[Deleting Source] Bucket %s deleted.\n",
                 ctx->status.bucket_name);
}


static void delete_source_bucket(struct cloudmig_ctx *ctx,
                                 struct bucket_status *bst)
{
    // Ptr used to change from bucket status filename to bucket name
    // ie: the '.' of "file.cloudmig"
    char                        *dotptr = strrchr(bst->filename, '.');
    struct file_state_entry     *fste = NULL; 
    dpl_status_t                dplret;

    if (dotptr == NULL) // though it should never happen...
        return ;

    cloudmig_log(DEBUG_LVL,
                 "[Deleting Source]: Deleting source bucket"
                 " for status file '%s'...\n",
                 bst->filename);

    // Here the buffer should never be allocated, so map the bucket state.
    if (cloudmig_map_bucket_state(ctx, bst) == EXIT_FAILURE)
        return ;

    *dotptr = '\0';
    // loop on the bucket state for each entry, to delete the files.
    while (bst->next_entry_off < bst->size)
    {
        fste = (void*)(bst->buf + bst->next_entry_off);
        delete_file(ctx, bst->filename, (char*)(fste+1));
        // Next entry...
        bst->next_entry_off += sizeof(*fste) + ntohl(fste->namlen);
    }

    free(bst->buf);

    /*
     * Remove bucket now that all of its files were deleted.
     */
    *dotptr = '\0';
    dplret = dpl_delete_bucket(ctx->src_ctx, bst->filename);
    if (dplret != DPL_SUCCESS)
    {
        /*
         * In case of an http 409 error (EEXIST or ENOENT),
         * do not do anything.
         * Maybe files were added in the bucket in the meantime ?
         * The user will have to manage it himself, it's his fault.
         */
        PRINTERR("%s: Could not remove bucket %s : %s.\n"
                 "The bucket may have been tampered with"
                 " since the migration's start.\n", __FUNCTION__,
                 bst->filename, dpl_status_str(dplret));
    }
    else
        cloudmig_log(DEBUG_LVL,
        "[Deleting Source]: Source bucket '%s' deleted successfully.\n",
        bst->filename);

    *dotptr = '.';
}


void delete_source(struct cloudmig_ctx *ctx)
{
    cloudmig_log(INFO_LVL,
    "[Deleting Source]: Starting deletion of the migration's source...\n");
    for (int i = 0; i < ctx->status.n_buckets; ++i)
        delete_source_bucket(ctx, &(ctx->status.buckets[i]));
    delete_status_bucket(ctx);
    cloudmig_log(INFO_LVL,
    "[Deleting Source]: Deletion of the migration's source done.\n");
}

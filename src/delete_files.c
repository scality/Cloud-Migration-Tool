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

static void delete_status_bucket(struct cloudmig_ctx *ctx)
{
    dpl_status_t    dplret;
    dpl_vec_t       *objects = NULL;

    dplret = dpl_list_bucket(ctx->src_ctx, ctx->status.bucket_name,
                             NULL, NULL, &objects, NULL);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not list bucket %s for deletion : %s\n",
                 __FUNCTION__, ctx->status.bucket_name, dpl_status_str(dplret));
        goto deletebucket;
    }

    cloudmig_log(DEBUG_LVL, "[Deleting Source] Deleting from bucket %s:\n",
                 ctx->status.bucket_name);
    dpl_object_t** cur_object = (dpl_object_t**)objects->array;
    for (int i = 0; i < objects->n_items; ++i, ++cur_object)
    {
        cloudmig_log(DEBUG_LVL, "[Deleting Source]\tfile : %s.\n",
                     (*cur_object)->key);
        dplret = dpl_delete(ctx->src_ctx,
                            ctx->status.bucket_name,
                            (*cur_object)->key,
                            NULL);
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("%s: Could not delete file %s from bucket %s : %s.\n",
                     __FUNCTION__, (*cur_object)->key, ctx->status.bucket_name,
                     dpl_status_str(dplret));
        }
    }

deletebucket:
    dpl_deletebucket(ctx->src_ctx, ctx->status.bucket_name);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not delete bucket %s : %s.\n",
                 __FUNCTION__, ctx->status.bucket_name, dpl_status_str(dplret));
        return ;
    }
    cloudmig_log(DEBUG_LVL, "[Deleting Source] Bucket %s deleted.\n",
                 ctx->status.bucket_name);
}


static void delete_file(struct cloudmig_ctx *ctx, char *bucket, char *filename)
{
    dpl_status_t dplret;

    dplret = dpl_delete(ctx->src_ctx, bucket, filename, NULL);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not delete the file %s" " from the bucket %s : %s",
                 __FUNCTION__, filename, bucket, dpl_status_str(dplret));
    }
}


static void delete_source_bucket(struct cloudmig_ctx *ctx,
                                 struct transfer_state *bucket_state)
{
    // Ptr used to change from bucket status filename to bucket name
    // ie: the '.' of "file.cloudmig"
    char                        *dotptr = strrchr(bucket_state->filename, '.');
    struct file_state_entry     *fste = NULL; 
    dpl_status_t                dplret;

    if (dotptr == NULL) // though it should never happen...
        return ;

    // Here the buffer should never be allocated, so map the bucket state.
    if (cloudmig_map_bucket_state(ctx, bucket_state) == EXIT_FAILURE)
        return ;

    *dotptr = '\0';
    // loop on the bucket state for each entry, to delete the files.
    while (bucket_state->next_entry_off < bucket_state->size)
    {
        // Most of this code would disappear with better binary file format
        fste = (void*)(bucket_state->buf + bucket_state->next_entry_off);
        char *filename = (char*)(fste+1);
        char *afterlast = filename + ntohl(fste->namlen);
        char save = *afterlast;

        *afterlast = '\0'; // Make sure there is a terminating '\0'
        delete_file(ctx, bucket_state->filename, filename);
        *afterlast = save; // restore the original data

        // Next entry...
        bucket_state->next_entry_off += sizeof(*fste) + ntohl(fste->namlen);
    }

    free(bucket_state->buf);

    /*
     * Remove bucket now that all of its files were deleted.
     */
    *dotptr = '\0';
    dplret = dpl_deletebucket(ctx->src_ctx, bucket_state->filename);
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
                 bucket_state->filename, dpl_status_str(dplret));
    }
    *dotptr = '.';
}


void delete_source(struct cloudmig_ctx *ctx)
{
    cloudmig_log(INFO_LVL,
    "[Deleting Source]: Starting deletion of the migration's source.\n");
    for (int i = 0; i < ctx->status.nb_states; ++i)
        delete_source_bucket(ctx, &(ctx->status.bucket_states[i]));
    delete_status_bucket(ctx);
}

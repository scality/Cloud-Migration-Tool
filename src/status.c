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
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

#include "cloudmig.h"

/*
 *
 * This file contains a collection of functions used to manipulate
 * the status file of the transfer.
 *
 *
 * Here is a list of static functions :
 * static char*         compute_status_bucket(struct cloudmig_ctx* ctx);
 * static int           create_status_bucket(struct cloudmig_ctx* ctx);
 * static size_t        calc_status_file_size(dpl_object_t** objs, int n_items);
 * static int           create_status_file(struct cloudmig_ctx* ctx,
 *                                         dpl_bucket_t* bucket);
 *
 * static int           status_retrieve_states(struct cloudmig_ctx* ctx);
 * static dpl_status_t  status_append_buffer_to_bucket_state(void* ctx,
 *                                                           char* buf,
 *                                                           unsigned int len);
 */

/*
 * Computes the name of the bucket used to store the transfer status files
 * depending on the destination and source, in order to avoid any bucket name
 * conflict.
 *
 * This function returns a buffer of allocated memory that has to be freed
 */
char*     compute_status_bucket(struct cloudmig_ctx* ctx)
{
    // Those should not be invalid.
    assert(ctx);
    assert(ctx->src_ctx);
    assert(ctx->src_ctx->host);
    assert(ctx->dest_ctx);
    assert(ctx->dest_ctx->host);

    char* name = 0;
    int len = 0;
    /*
     * result string will be of the form :
     * "cloudmig_srchostname_to_desthostname"
     */
    len = 9 + strlen(ctx->src_ctx->host) + 4 + strlen(ctx->dest_ctx->host);
    if (len < 255)
    {
        if ((name = calloc(sizeof(*name), len + 1)) == NULL)
        {
            PRINTERR("%s: Could not compute status bucket name : %s\n",
                     __FUNCTION__, strerror(errno));
            return (NULL);
        }
        strcpy(name, "cloudmig.");
        strcat(name, ctx->src_ctx->host);
        strcat(name, ".to.");
        strcat(name, ctx->dest_ctx->host);
    }
    else
        return (strdup("cloudmig.status"));
    return (name);
}

int load_status(struct cloudmig_ctx* ctx)
{
     assert(ctx);
    int             ret = EXIT_SUCCESS;
    dpl_status_t    dplret;
    int             resuming = 0; // differentiate resuming and starting mig'
    dpl_vec_t       *src_buckets = NULL;

    // First, make sure we have a status bucket defined.
    if (ctx->status.bucket_name == NULL)
        ctx->status.bucket_name = compute_status_bucket(ctx);
    if (ctx->status.bucket_name == NULL)
        return (EXIT_FAILURE);

    /*
     * Now retrieve the bucket list for the source account
     * in order to check if the status bucket already exists or not
     *
     * By the way, we do not care about the destination's bucket list,
     * since the user should know that the tool makes a forceful copy.
     */
    dplret = dpl_list_all_my_buckets(ctx->src_ctx, &src_buckets);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not list source's buckets : %s\n",
                 __FUNCTION__, dpl_status_str(dplret));
        goto err;
    }
    for (int i = 0; i < src_buckets->n_items; ++i)
    {
        if (strcmp(((dpl_bucket_t**)(src_buckets->array))[i]->name,
                   ctx->status.bucket_name) == 0)
        {
            cloudmig_log(DEBUG_LVL,
"[Creating status] Found status bucket (%s) on source storage\n",
                         ctx->status.bucket_name);
            resuming = 1;
            break ;
        }
    }
    if (resuming == 0)// Then we must build the new status file through
    {
        if ((ret = create_status(ctx, src_buckets)))
        {
            PRINTERR("%s: Could not create the migration's status.\n",
                     __FUNCTION__);
            goto err;
        }
    }
    // The status bucket IS created, filled and clean.

    /*
     * create the buckets states in order to be able
     * to follow the states evolutions.
     */
    if (status_retrieve_states(ctx))
        goto err;

    goto end;


err:
    ret = EXIT_FAILURE;

    if (ctx->status.bucket_name != NULL)
    {
        free(ctx->status.bucket_name);
        ctx->status.bucket_name = NULL;
    }

end:
    if (src_buckets != NULL)
        dpl_vec_buckets_free(src_buckets);

    return ret;
}


/*
 * Callback function for dpl_openread (needs a callback for each chunk
 * of data received)
 *
 * It appends each chunk of data to the buf of the transfer_state
 */
static dpl_status_t status_append_buffer_to_bucket_state(void* ctx,
                                                         char* buf,
                                                         unsigned int len)
{
    struct transfer_state* state = ctx;
    dpl_status_t    ret = DPL_FAILURE;

    int newlen = state->next_entry_off + len;
    state->buf = realloc(state->buf, newlen);
    if (state->buf == NULL)
    {
        ret = DPL_ENOMEM;
        goto err;
    }
    memmove(state->buf + state->next_entry_off, buf, len);
    state->next_entry_off += len;

    ret = DPL_SUCCESS;

err:
    
    return ret;
}

/*
 * Maps a bucket state's file into memory using dpl_openread.
 */
static int status_map_bucket_state(struct cloudmig_ctx* ctx,
                                   struct transfer_state* bucket_state)
{
    dpl_status_t        dplret;
    int                 ret = EXIT_FAILURE;
    dpl_dict_t          *metadata;
    char*               ctx_bucket = ctx->src_ctx->cur_bucket;

    /*
     * This function uses the bucket_state as private data for the callback.
     *
     * It uses the field next_entry_off to set the quantity of data received
     * so it needs to be reset to 0 afterwards.
     */
    ctx->src_ctx->cur_bucket = ctx->status.bucket_name;
    dplret = dpl_openread(ctx->src_ctx, bucket_state->filename,
                          DPL_VFILE_FLAG_MD5, NULL,
                          &status_append_buffer_to_bucket_state, bucket_state,
                          &metadata);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not map bucket status file %s : %s\n",
                 __FUNCTION__, bucket_state->filename, dpl_status_str(dplret));
        goto err;
    }

    ret = EXIT_SUCCESS;

err:
    // Restore original bucket.
    ctx->src_ctx->cur_bucket = ctx_bucket;

    bucket_state->next_entry_off = 0;

    if (metadata != NULL)
        dpl_dict_free(metadata);

    return ret;
}


/*
 * The function allocates and retrieves the content of a bucket state file
 * if it is to be read.
 * It also frees the content of the bucket state files it already went over.
 *
 * 
 */
int status_next_incomplete_entry(struct cloudmig_ctx* ctx,
                                 struct file_transfer_state* filestate,
                                 char** bucket)
{
    assert(ctx != NULL);
    assert(ctx->status.bucket_name != NULL);

    int                     ret = EXIT_FAILURE;
    struct file_state_entry *fste;

    /*
     * For each file in the status bucket, read it entry by entry
     * and stop when coming across an incomplete (/not migrated) entry.
     *
     * Begin at cur_state, and then next_entry_off in the buffer.
     * If the buffer is not allocated, it's time to do it.
     */
    for (int i = ctx->status.cur_state; i < ctx->status.nb_states; ++i)
    {
        struct transfer_state*  bucket_state = &(ctx->status.bucket_states[i]);
        if (bucket_state->buf == NULL
            && status_map_bucket_state(ctx, bucket_state) == EXIT_FAILURE)
            goto err;
        // loop on the bucket state for each entry, until the end.
        while (bucket_state->next_entry_off < bucket_state->size)
        {
            fste = (void*)(bucket_state->buf + bucket_state->next_entry_off);
            /*
             * Check if this file has yet to be transfered
             *
             * Here we have to permanently use ntohl over fste's values,
             * since the int32_t are stored in network byte order.
             */
            if (ntohl(fste->offset) < ntohl(fste->size))
            {
                // First, fill the fste struct...
                filestate->fixed.size = ntohl(fste->size);
                filestate->fixed.offset = ntohl(fste->offset);
                filestate->fixed.namlen = ntohl(fste->namlen);
                // Use pointer arithmetics to get the name after the fste
                filestate->name = strdup((char*)(fste+1));
                if (filestate->name == NULL)
                {
                    PRINTERR("%s: could not allocate memory: %s",
                             __FUNCTION__, strerror(errno));
                    goto err;
                }

                // Second : Copy the bucket status file name and truncate it
                if ((*bucket = strdup(bucket_state->filename)) == NULL)
                {
                    PRINTERR("%s: could not allocate memory: %s",
                             __FUNCTION__, strerror(errno));
                    goto err;
                }
                // Cut the string at the ".cloudmig" part to get the bucket
                // Here the filename is valid, so it should never crash :
                *(strrchr(*bucket, '.')) = '\0';

                bucket_state->next_entry_off += sizeof(*fste)
                                                + filestate->fixed.namlen;
                break ;
            }
            bucket_state->next_entry_off += sizeof(*fste) + ntohl(fste->namlen);
        }

        if (bucket_state->next_entry_off < bucket_state->size)
            break ;

        free(ctx->status.bucket_states[i].buf);
        ctx->status.bucket_states[i].buf = NULL;
        ctx->status.cur_state = i; // update the current state file index.
    }

    ret = EXIT_SUCCESS;
    if (ctx->status.cur_state == ctx->status.nb_states)
        ret = ENODATA;

    return ret;

err:
    if(filestate->name)
    {
        free(filestate->name);
        filestate->name = NULL;
    }
    filestate->fixed.size = 0;
    filestate->fixed.offset = 0;
    filestate->fixed.namlen = 0;
    if (*bucket)
    {
        free(*bucket);
        *bucket = NULL;
    }

    return ret;
}

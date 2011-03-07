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

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "cloudmig.h"

struct migration_status
{
    char    *buf;
    size_t  size;
    size_t  offset;
};

dpl_status_t    migst_cb(void *data, char *buf, unsigned int len)
{
    assert(((struct migration_status*)data)->offset + len
           <= ((struct migration_status*)data)->size);


    memcpy(((struct migration_status*)data)->buf
            + ((struct migration_status*)data)->offset, buf, len);
    ((struct migration_status*)data)->offset += len;
    return DPL_SUCCESS;
}

static int status_retrieve_associated_buckets(struct cloudmig_ctx* ctx,
                                              size_t fsize)
{
    int                         ret = EXIT_FAILURE;
    dpl_status_t                dplret;
    dpl_dict_t                  *metadata = NULL;
    struct migration_status     status = {NULL, fsize, 0};
    struct cldmig_state_entry   *entry = NULL;

    cloudmig_log(INFO_LVL,
                 "[Loading Status]: Retrieving src/dest buckets associations...\n");
    status.buf = calloc(fsize, sizeof(*status.buf));
    if (status.buf == NULL)
    {
        PRINTERR("%s: Could not allocate memory for migration status buffer.\n",
                 __FUNCTION__);
        goto end;
    }

    dplret = dpl_openread(ctx->src_ctx, ".cloudmig",
                          DPL_VFILE_FLAG_MD5,
                          NULL,
                          &migst_cb, &status,
                          &metadata);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not read the general migration status file: %s.\n",
                 __FUNCTION__, dpl_status_str(dplret));
        goto end;
    }

    /*
     * Now that we mapped the status file,
     * Let's read it and associate bucket status files with
     * the destination buckets.
     */

    for (entry = (void*)status.buf;
         (long int)entry < (long int)(status.buf + status.size);
         entry = (void*)((char*)(entry) + sizeof(*entry)
                         + ntohl(entry->file) + ntohl(entry->bucket)))
    {
        cloudmig_log(DEBUG_LVL,
                     "[Loading Status]: searching match for status file %.*s.\n",
                     ntohl(entry->file), (char*)(entry+1));
        // Match the current entry with the right bucket_state.
        for (int i=0; i < ctx->status.nb_states; ++i)
        {
            // Is it the right one ?
            if (!strncmp((char*)(entry+1),
                         ctx->status.bucket_states[i].filename,
                         ntohl(entry->file)))
            {
                // go over the filename to copy the destination bucket name
                ctx->status.bucket_states[i].dest_bucket =
                    calloc(entry->file, sizeof(char));
                if (ctx->status.bucket_states[i].dest_bucket == NULL)
                {
                    PRINTERR("%s: Could not allocate memory while"
                             " loading status...\n",
                             __FUNCTION__,0);
                    goto end;
                }
                strncpy(ctx->status.bucket_states[i].dest_bucket,
                        (char*)entry + sizeof(*entry) + ntohl(entry->file),
                        ntohl(entry->bucket));
                cloudmig_log(DEBUG_LVL,
                "[Loading Status]: matched status file %s to dest bucket %s.\n",
                ctx->status.bucket_states[i].filename,
                ctx->status.bucket_states[i].dest_bucket);
                break ;
            }
        }
    }

    ret = EXIT_SUCCESS;

end:
    if (status.buf)
        free(status.buf);
    if (metadata)
        dpl_dict_free(metadata);
    return ret;
}


int status_retrieve_states(struct cloudmig_ctx* ctx)
{
    assert(ctx != NULL);
    assert(ctx->status.bucket_name != NULL);
    assert(ctx->status.bucket_states == NULL);

    dpl_status_t            dplret = DPL_SUCCESS;
    int                     ret = EXIT_FAILURE;
    dpl_vec_t               *objects;
    size_t                  migstatus_size = 0;

    ctx->src_ctx->cur_bucket = ctx->status.bucket_name;

    cloudmig_log(INFO_LVL, "[Loading Status]: Retrieving status...\n");
    // Retrieve the list of files for the buckets states
    dplret = dpl_list_bucket(ctx->src_ctx, ctx->status.bucket_name,
                             NULL, NULL, &objects, NULL);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not list status bucket's files: %s\n",
                 __FUNCTION__, ctx->status.bucket_name, dpl_status_str(dplret));
        goto err;
    }

    // Allocate enough room for each bucket_state.
    ctx->status.nb_states = objects->n_items;
    ctx->status.cur_state = 0;
    // -1 cause we dont want to allocate an entry for ".cloudmig"
    ctx->status.bucket_states = calloc(objects->n_items,
                                       sizeof(*(ctx->status.bucket_states)));
    if (ctx->status.bucket_states == NULL)
    {
        PRINTERR("%s: Could not allocate state data for each bucket: %s\n",
                 __FUNCTION__, strerror(errno));
        goto err;
    }

    // Now fill each one of these structures
    dpl_object_t** objs = (dpl_object_t**)objects->array;
    int i_bucket = 0;
    for (int i=0; i < objects->n_items; ++i, ++i_bucket)
    {
        if (strcmp(".cloudmig", objs[i]->key) == 0)
        {
            // save the file size
            migstatus_size = objs[i]->size;
            // fix the nb_states of the status ctx
            --ctx->status.nb_states;
            // Now get to next entry without advancing in the bucket_states.
            ++i;
            if (i >= objects->n_items)
                break ;
        }
        ctx->status.bucket_states[i_bucket].filename = strdup(objs[i]->key);
        if (ctx->status.bucket_states[i_bucket].filename == NULL)
        {
            PRINTERR("%s: Could not allocate state data for each bucket: %s\n",
                     __FUNCTION__, strerror(errno));
            goto err;
        }
        ctx->status.bucket_states[i_bucket].size = objs[i]->size;
        ctx->status.bucket_states[i_bucket].next_entry_off = 0;
        // The buffer will be read/allocated when needed.
        // Otherwise, it may use up too much memory
        ctx->status.bucket_states[i_bucket].buf = NULL;
    }

    if (status_retrieve_associated_buckets(ctx, migstatus_size) == EXIT_FAILURE)
    {
        PRINTERR("%s: Could not associate status files to dest buckets.\n",
                 __FUNCTION__);
        goto err;
    }

    ret = EXIT_SUCCESS;

err:
    if (ret == EXIT_FAILURE && ctx->status.bucket_states != NULL)
    {
        for (int i=0; i < ctx->status.nb_states; ++i)
        {
            if (ctx->status.bucket_states[i].filename)
                free(ctx->status.bucket_states[i].filename);
        }
        free(ctx->status.bucket_states);
        ctx->status.bucket_states = NULL;
    }

    if (objects != NULL)
        dpl_vec_objects_free(objects);

    ctx->src_ctx->cur_bucket = NULL;

    return ret;
}

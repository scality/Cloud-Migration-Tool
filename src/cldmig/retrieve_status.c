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
#include <endian.h>
#include <string.h>
#include <unistd.h>

#include "cloudmig.h"

#include <droplet/vfs.h>

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
    struct cldmig_state_entry   *entry = NULL;
    struct migration_status     status = {NULL, fsize, 0};

    cloudmig_log(INFO_LVL,
                 "[Loading Status]: Retrieving source/destination"
                 " buckets associations...\n");

    status.buf = calloc(fsize, sizeof(*status.buf));
    if (status.buf == NULL)
    {
        PRINTERR("%s: Could not allocate memory for migration status buffer.\n",
                 __FUNCTION__);
        goto end;
    }
    ctx->status.general.size = fsize;
    ctx->status.general.buf = status.buf;

    dplret = dpl_openread(ctx->src_ctx, ".cloudmig",
                          DPL_VFILE_FLAG_MD5,
                          NULL/*cond*/, NULL/*range*/,
                          &migst_cb, &status,
                          &metadata, NULL/*sysmd*/);
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
    // Switch from big endian 64 to host endian
    ctx->status.general.head.total_sz =
        be64toh(((struct cldmig_state_header*)status.buf)->total_sz);
    ctx->status.general.head.done_sz =
        be64toh(((struct cldmig_state_header*)status.buf)->done_sz);
    ctx->status.general.head.nb_objects =
        be64toh(((struct cldmig_state_header*)status.buf)->nb_objects);
    ctx->status.general.head.done_objects =
        be64toh(((struct cldmig_state_header*)status.buf)->done_objects);
    // Now map the matching buckets
    for (entry = (void*)status.buf + sizeof(struct cldmig_state_header);
         (long int)entry < (long int)(status.buf + status.size);
         entry = (void*)((char*)(entry) + sizeof(*entry)
                         + ntohl(entry->file) + ntohl(entry->bucket)))
    {
        cloudmig_log(DEBUG_LVL,
            "[Loading Status]: searching match for status file %.*s.\n",
            ntohl(entry->file), (char*)(entry+1));

        // Match the current entry with the right bucket.
        for (int i=0; i < ctx->status.n_buckets; ++i)
        {
            // Is it the right one ?
            if (!strcmp((char*)(entry+1),
                        ctx->status.buckets[i].filename))
            {
                // copy the destination bucket name
                ctx->status.buckets[i].dest_bucket =
                    strdup((char*)entry + sizeof(*entry) + ntohl(entry->file));
                if (ctx->status.buckets[i].dest_bucket == NULL)
                {
                    PRINTERR("%s: Could not allocate memory while"
                             " loading status...\n", __FUNCTION__);
                    goto end;
                }

                cloudmig_log(DEBUG_LVL,
                "[Loading Status]: matched status file %s to dest bucket %s.\n",
                    ctx->status.buckets[i].filename,
                    ctx->status.buckets[i].dest_bucket);

                break ;
            }
        }
    }

    ret = EXIT_SUCCESS;
    cloudmig_log(INFO_LVL,
        "[Loading Status]: Source/Destination"
        " buckets associations done.\n");

    status.buf = 0;

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
    assert(ctx->status.buckets == NULL);

    dpl_status_t            dplret = DPL_SUCCESS;
    int                     ret = EXIT_FAILURE;
    dpl_vec_t               *objects;
    size_t                  migstatus_size = 0;

    ctx->src_ctx->cur_bucket = ctx->status.bucket_name;

    cloudmig_log(INFO_LVL, "[Loading Status]: Retrieving status...\n");

    // Retrieve the list of files for the buckets states
    dplret = dpl_list_bucket(ctx->src_ctx, ctx->status.bucket_name,
                             NULL, NULL, -1, &objects, NULL);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not list status bucket's files: %s\n",
                 __FUNCTION__, ctx->status.bucket_name, dpl_status_str(dplret));
        goto err;
    }

    // Allocate enough room for each bucket.
    ctx->status.n_buckets = objects->n_items;
    ctx->status.cur_state = 0;
    // -1 cause we dont want to allocate an entry for ".cloudmig"
    ctx->status.buckets = calloc(objects->n_items,
                                 sizeof(*(ctx->status.buckets)));
    if (ctx->status.buckets == NULL)
    {
        PRINTERR("%s: Could not allocate state data for each bucket: %s\n",
                 __FUNCTION__, strerror(errno));
        goto err;
    }

    // Now fill each one of these structures
    int i_bucket = 0;
    for (int i=0; i < objects->n_items; ++i, ++i_bucket)
    {
	dpl_object_t* obj = (dpl_object_t*)(objects->items[i]->ptr);
        if (strcmp(".cloudmig", obj->path) == 0)
        {
            // save the file size
            migstatus_size = obj->size;
            // fix the n_buckets of the status ctx
            --ctx->status.n_buckets;
            // Now get to next entry without advancing in the buckets.
            ++i;
            if (i >= objects->n_items)
                break ;
        }
        ctx->status.buckets[i_bucket].filename = strdup(obj->path);
        if (ctx->status.buckets[i_bucket].filename == NULL)
        {
            PRINTERR("%s: Could not allocate state data for each bucket: %s\n",
                     __FUNCTION__, strerror(errno));
            goto err;
        }
        ctx->status.buckets[i_bucket].size = obj->size;
        ctx->status.buckets[i_bucket].next_entry_off = 0;
        // The buffer will be read/allocated when needed.
        // Otherwise, it may use up too much memory
        ctx->status.buckets[i_bucket].buf = NULL;
    }

    if (status_retrieve_associated_buckets(ctx, migstatus_size) == EXIT_FAILURE)
    {
        PRINTERR("%s: Could not associate status files to dest buckets.\n",
                 __FUNCTION__);
        goto err;
    }

    ret = EXIT_SUCCESS;

    cloudmig_log(INFO_LVL, "[Loading Status]: Status data retrieved.\n");

err:
    if (ret == EXIT_FAILURE && ctx->status.buckets != NULL)
    {
        for (int i=0; i < ctx->status.n_buckets; ++i)
        {
            if (ctx->status.buckets[i].filename)
                free(ctx->status.buckets[i].filename);
        }
        free(ctx->status.buckets);
        ctx->status.buckets = NULL;
    }

    if (objects != NULL)
        dpl_vec_objects_free(objects);

    ctx->src_ctx->cur_bucket = NULL;

    return ret;
}

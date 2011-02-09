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
 */

/*
 * Computes the name of the bucket used to store the transfer status files
 * depending on the destination and source, in order to avoid any bucket name
 * conflict.
 *
 * This function returns a buffer of allocated memory that has to be freed
 */
static char*     compute_status_bucket(struct cloudmig_ctx* ctx)
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

static int create_status_bucket(struct cloudmig_ctx* ctx)
{
    assert(ctx != NULL);
    assert(ctx->status.bucket_name != NULL);

    dpl_status_t    ret;

    ret = dpl_make_bucket(ctx->src_ctx, ctx->status.bucket_name,
                          DPL_LOCATION_CONSTRAINT_US_STANDARD,
                          DPL_CANNED_ACL_PRIVATE);
    if (ret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not create status bucket '%s' (%i bytes): %s\n",
                 __FUNCTION__, ctx->status.bucket_name, strlen(ctx->status.bucket_name),
                 dpl_status_str(ret));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static size_t calc_status_file_size(dpl_object_t** objs, int n_items)
{
    size_t size = 0;
    for (int i = 0; i < n_items; ++i, ++objs)
    {
        int len = strlen((*objs)->key);
        size += sizeof(struct file_state_entry); // fixed state entry data
        size += ROUND_NAMLEN(len); // namlen rounded to superior 4
    }
    return size;
}

static int create_status_file(struct cloudmig_ctx* ctx, dpl_bucket_t* bucket)
{
    assert(ctx != NULL);
    assert(ctx->src_ctx != NULL);
    assert(bucket != NULL);

    int             ret = EXIT_SUCCESS;
    dpl_status_t    dplret = DPL_SUCCESS;
    dpl_vec_t*      objects = NULL;
    dpl_vfile_t*    bucket_status = NULL;
    char            *ctx_bucket;
    // Data for the bucket status file
    int             namlen;
    size_t          filesize;
    char            *filename = NULL;
    // Data for each entry of the bucket status file
    struct file_state_entry entry;
    char            *entry_filename = NULL;

    if ((dplret = dpl_list_bucket(ctx->src_ctx, bucket->name,
                                  NULL, NULL, &objects, NULL)) != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not list bucket %s : %s\n", __FUNCTION__,
                 bucket->name, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto end;
    }


    namlen = strlen(bucket->name) + 10;
    filename = malloc(namlen * sizeof(*filename));
    if (filename == NULL)
    {
        PRINTERR("%s: Could not save bucket '%s' status : %s\n",
                 __FUNCTION__, bucket->name, strerror(errno));
        ret = EXIT_FAILURE;
        goto end;
    }
    strcpy(filename, bucket->name);
    strcat(filename, ".cloudmig");
    cloudmig_log(DEBUG_LVL, "[Creating status] Filename for bucket %s : %s\n",
                 bucket->name, filename);


    // Save the bucket and set the cloudmig_status bucket as current.
    ctx_bucket = ctx->src_ctx->cur_bucket;
    ctx->src_ctx->cur_bucket = ctx->status.bucket_name;
    filesize = calc_status_file_size((dpl_object_t**)(objects->array),
                                     objects->n_items);
    if ((dplret = dpl_openwrite(ctx->src_ctx, filename, DPL_VFILE_FLAG_CREAT,
                                NULL, DPL_CANNED_ACL_PRIVATE, filesize,
                                &bucket_status)))
    {
        PRINTERR("%s: Could not create bucket %s's status file : %s\n",
                 __FUNCTION__, bucket->name, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto end;
    }

    /*
     * For each file, write the part matching the file.
     */
    dpl_object_t** cur_object = (dpl_object_t**)objects->array;
    cloudmig_log(DEBUG_LVL,
                 "[Creating status] Bucket %s (%i objects) in file '%s':\n",
                 bucket->name, objects->n_items, filename);
    for (int i = 0; i < objects->n_items; ++i, ++cur_object)
    {
        cloudmig_log(DEBUG_LVL, "[Creating status] \t file : '%s'(%i bytes)\n",
                     (*cur_object)->key, (*cur_object)->size);
        int len = strlen((*cur_object)->key);
        entry.namlen = ROUND_NAMLEN(len);
        entry.size = (*cur_object)->size;
        entry.offset = 0;
        entry_filename = calloc(entry.namlen, sizeof(*entry_filename));
        if (entry_filename == NULL)
        {
            PRINTERR("%s: Could not allocate file state entry : %s\n",
                     __FUNCTION__, strerror(errno));
            ret = EXIT_FAILURE;
            goto end;
        }
        // already padded with zeroes, copy only the string
        memcpy(entry_filename, (*cur_object)->key, len);
        // translate each integer into network byte order
        entry.namlen = htonl(entry.namlen);
        entry.size = htonl(entry.size);
        entry.offset = htonl(entry.offset);
        dplret = dpl_write(bucket_status, (char*)(&entry), sizeof(entry));
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("%s: Could not send file entry: %s\n",
                     __FUNCTION__, dpl_status_str(dplret));
            ret = EXIT_FAILURE;
            goto end;
        }
        dplret = dpl_write(bucket_status, entry_filename, ROUND_NAMLEN(len));
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("%s: Could not send file entry: %s\n",
                     __FUNCTION__, dpl_status_str(dplret));
            ret = EXIT_FAILURE;
            goto end;
        }
    }
    cloudmig_log(DEBUG_LVL, "[Creating status] Bucket %s: SUCCESS.\n",
                 bucket->name);

    // Now that's done, free the memory allocated by the libdroplet
    // And restore the source ctx's cur_bucket
end:
    if (entry_filename != NULL)
        free(entry_filename);

    if (bucket_status != NULL)
        dpl_close(bucket_status);

    if (filename != NULL)
        free(filename);

    if (objects != NULL)
        dpl_vec_objects_free(objects);

    ctx->src_ctx->cur_bucket = ctx_bucket;
    return ret;
}


int load_status(struct cloudmig_ctx* ctx)
{
    assert(ctx);
    int             ret = EXIT_SUCCESS;
    dpl_status_t    dplret;
    int             resuming = 0; // Used to differentiate resuming migration from starting it

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
    dpl_vec_t*  src_buckets = 0;
    if ((dplret = dpl_list_all_my_buckets(ctx->src_ctx,
                                       &src_buckets)) != DPL_SUCCESS)
    {
        
        PRINTERR("%s: Could not list source's buckets : %s\n",
                 __FUNCTION__, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto free_status_name;
    }
    for (int i = 0; i < src_buckets->n_items; ++i)
    {
        if (strcmp(((dpl_bucket_t**)(src_buckets->array))[i]->name,
                   ctx->status.bucket_name) == 0)
        {
            cloudmig_log(DEBUG_LVL, "Found status bucket (%s) on source storage\n",
                         ctx->status.bucket_name);
            resuming = 1;
            break ;
        }
    }
    if (resuming == 0)// Then we must build the new status file through
    {
        // First, create the status bucket since it didn't exist.
        if ((ret = create_status_bucket(ctx)))
        {
            PRINTERR("%s: Could not create status bucket\n", __FUNCTION__);
            goto free_buckets_vec;
        }

        /*
         * For each bucket, we create a file named after it in the
         * status bucket.
         */
        dpl_bucket_t**  cur_bucket = (dpl_bucket_t**)src_buckets->array;
        for (int i = 0; i < src_buckets->n_items; ++i, ++cur_bucket)
        {
            if ((ret = create_status_file(ctx, *cur_bucket)))
            {
                cloudmig_log(WARN_LVL,
                             "An Error happened while creating the status bucket and file.\nPlease delete manually the bucket '%s' before restarting the tool...\n",
                             ctx->status.bucket_name);
                goto free_buckets_vec;
            }
        }
    }

    // Now, set where we are going to start/resume the migration from




free_buckets_vec:
    dpl_vec_buckets_free(src_buckets);

free_status_name:
    free(ctx->status.bucket_name);
    ctx->status.bucket_name = NULL;

    return ret;
}

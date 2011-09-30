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
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cloudmig.h"
#include "options.h"

static int create_status_bucket(struct cloudmig_ctx* ctx)
{
    assert(ctx != NULL);
    assert(ctx->status.bucket_name != NULL);

    dpl_status_t    ret;
    
    cloudmig_log(DEBUG_LVL, "[Creating Status]: Creating status bucket...\n");

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

static int fill_entry_from_object(struct file_state_entry* entry,
                                  char** filename,
                                  dpl_object_t *obj)
{
    int len = strlen(obj->key);

    entry->namlen = ROUND_NAMLEN(len);
    entry->size = obj->size;
    entry->offset = 0;
    *filename = calloc(entry->namlen, sizeof(**filename));
    if (*filename == NULL)
    {
        PRINTERR("%s: Could not allocate file state entry->: %s\n",
                 __FUNCTION__, strerror(errno));
        return EXIT_FAILURE;
    }
    // already padded with zeroes, copy only the string
    memcpy(*filename, obj->key, len);

    // translate each integer into network byte order for sending...
    entry->namlen = htonl(entry->namlen);
    entry->size = htonl(entry->size);
    entry->offset = htonl(entry->offset);

    return EXIT_SUCCESS;
}

static int create_status_file(struct cloudmig_ctx* ctx,
                              char* bucket_name,
                              char **filename)
{
    assert(ctx != NULL);
    assert(ctx->src_ctx != NULL);
    assert(bucket_name != NULL);

    int             ret = EXIT_FAILURE;
    dpl_status_t    dplret = DPL_SUCCESS;
    dpl_vec_t*      objects = NULL;
    dpl_vfile_t*    bucket_status = NULL;
    char            *ctx_bucket;
    // Data for the bucket status file
    size_t          filesize;
    // Data for each entry of the bucket status file
    struct file_state_entry entry;
    char            *entry_filename = NULL;

    cloudmig_log(DEBUG_LVL,
                 "[Creating Status]: Creating status file for bucket '%s'...\n",
                 bucket_name);

    if ((dplret = dpl_list_bucket(ctx->src_ctx, bucket_name,
                                  NULL, NULL, &objects, NULL)) != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not list bucket %s : %s\n", __FUNCTION__,
                 bucket_name, dpl_status_str(dplret));
        goto end;
    }


    *filename = cloudmig_get_status_filename_from_bucket(bucket_name);
    if (*filename == NULL)
    {
        PRINTERR("%s: Could not save bucket '%s' status : %s\n",
                 __FUNCTION__, bucket_name, strerror(errno));
        goto end;
    }

    cloudmig_log(DEBUG_LVL, "[Creating status] Filename for bucket %s : %s\n",
                 bucket_name, *filename);

    // Save the bucket and set the cloudmig_status bucket as current.
    ctx_bucket = ctx->src_ctx->cur_bucket;
    ctx->src_ctx->cur_bucket = ctx->status.bucket_name;

    filesize = calc_status_file_size((dpl_object_t**)(objects->array),
                                     objects->n_items);
    if ((dplret = dpl_openwrite(ctx->src_ctx, *filename, DPL_VFILE_FLAG_CREAT,
                                NULL, DPL_CANNED_ACL_PRIVATE, filesize,
                                &bucket_status)))
    {
        PRINTERR("%s: Could not create bucket %s's status file : %s\n",
                 __FUNCTION__, bucket_name, dpl_status_str(dplret));
        goto end;
    }

    /*
     * For each file, write the part matching the file.
     */
    dpl_object_t** cur_object = (dpl_object_t**)objects->array;
    cloudmig_log(DEBUG_LVL,
                 "[Creating status] Bucket %s (%i objects) in file '%s':\n",
                 bucket_name, objects->n_items, *filename);
    ctx->status.general.head.nb_objects += objects->n_items;
    for (int i = 0; i < objects->n_items; ++i, ++cur_object)
    {
        cloudmig_log(DEBUG_LVL, "[Creating status] \t file : '%s'(%i bytes)\n",
                     (*cur_object)->key, (*cur_object)->size);

        // Prepare data for writing...
        if (fill_entry_from_object(&entry, &entry_filename,
                                   *cur_object) != EXIT_SUCCESS)
            goto end;
        ctx->status.general.head.total_sz += (*cur_object)->size;

        // Write data to network file
        dplret = dpl_write(bucket_status, (char*)(&entry), sizeof(entry));
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("%s: Could not send file entry: %s\n",
                     __FUNCTION__, dpl_status_str(dplret));
            goto end;
        }
        dplret = dpl_write(bucket_status, entry_filename, ntohl(entry.namlen));
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("%s: Could not send file entry: %s\n",
                     __FUNCTION__, dpl_status_str(dplret));
            goto end;
        }
    }
    cloudmig_log(DEBUG_LVL, "[Creating status] Bucket %s: SUCCESS.\n",
                 bucket_name);

    ret = EXIT_SUCCESS;

    // Now that's done, free the memory allocated by the libdroplet
    // And restore the source ctx's cur_bucket
end:
    if (entry_filename != NULL)
        free(entry_filename);

    if (bucket_status != NULL)
        dpl_close(bucket_status);

    if (objects != NULL)
        dpl_vec_objects_free(objects);

    ctx->src_ctx->cur_bucket = ctx_bucket;
    return ret;
}


/* Create a new list elem to write to the general status file afterwards */
static int append_to_stlist(struct cldmig_entry **list,
                            char **fname, char **bname)
{
    struct cldmig_entry *new_st = NULL;
    struct cldmig_entry *tmp_st = NULL;

    new_st = calloc(1, sizeof(*new_st));
    if (new_st == NULL)
    {
        PRINTERR(
"Could not create entry in the migration status file for status file %s: %s.\n",
                 fname, strerror(errno));
        return EXIT_FAILURE;
    }
    new_st->namlens.file = ROUND_NAMLEN(strlen(*fname));
    new_st->namlens.bucket = ROUND_NAMLEN(strlen(*bname));
    new_st->filename = *fname;
    new_st->bucket = *bname;
    new_st->next = 0;
    *fname = 0;
    *bname = 0;
    if (*list == 0)
        *list = new_st;
    else 
    {
        for (tmp_st = *list; tmp_st->next != 0; tmp_st=tmp_st->next)
            ;
        tmp_st->next = new_st;
    }
    new_st = 0;
    return EXIT_SUCCESS;
}


/*
 * the bname parameter points on the destinatino bucket's name if valid.
 * It will be changed/computed if invalid/not available.
 */
static int create_destination_bucket(struct cloudmig_ctx *ctx,
                                     char **bname)
{
    int             ret = EXIT_FAILURE;
    bool            retried = false;
    dpl_status_t    dplret;

    cloudmig_log(DEBUG_LVL,
                 "[Creating Status]: Creating destination bucket"
                 " %s...\n",
                 *bname);

    /*
     * Here we want to create a bucket with the same attributes as the
     * source bucket. (acl and location constraints)
     *
     *  - Get the location constraint
     *  - Get the acl
     *  - Create the new bucket with appropriate informations.
     */
    // TODO FIXME with correct attributes
    // For now, let's use some defaults caracs :
retry_mb_with_patched_name:
    dplret = dpl_make_bucket(ctx->dest_ctx, *bname,
                          DPL_LOCATION_CONSTRAINT_US_STANDARD,
                          DPL_CANNED_ACL_PRIVATE);
    if (dplret != DPL_SUCCESS)
    {
        /*
         * A bucket is an unique identifier on a storage space,
         * independant from the users. So two users cannot have the same
         * bucket name. Therefore, a tweak is necessarry.
         *
         * DPL_ENOENT and DPL_EEXIST for compatibility with droplet versions
         * (main head currently does not offer EEXIST but only ENOENT)
         */
        if ((dplret == DPL_ENOENT || dplret == DPL_EEXIST)
            && retried == false)
        {
            /*
             * First, workaround the pool connection issue of libdroplet
             * (issue #56 on github issues)
             */
            {
                if (dpl_make_bucket(ctx->dest_ctx, *bname,
                                    DPL_LOCATION_CONSTRAINT_US_STANDARD,
                                    DPL_CANNED_ACL_PRIVATE) == DPL_SUCCESS)
                {
                    PRINTERR("[WORKAROUND]: %s: Dummy request didn't fail???\n",
                             __FUNCTION__);
                }
                else
                {
                    PRINTERR("[WORKAROUND]: %s: Dummy Request"
                             " failed as expected...\n",
                             __FUNCTION__);
                }
            }

            // Fix the bucket's name by prepending cloudmig_timestamp_
            char *tmp;
            time_t  t = time(NULL);
            if (asprintf(&tmp, "cloudmig-%li-%s", t, *bname) == -1)
            {
                PRINTERR("%s: Could not create dest bucket : %s.\n",
                         "[Create Dest Buckets]", strerror(errno));
                goto err;
            }
            free(*bname);
            *bname = tmp;
            retried = true;
            cloudmig_log(INFO_LVL,
    "Could not create the bucket's copy. Re-trying with hashed name\n");
            goto retry_mb_with_patched_name;
        }
        PRINTERR("%s: Could not create destination bucket %s: %s\n",
                 "[Create Dest Buckets]", *bname, dpl_status_str(dplret));
        goto err;
    }

    ret = EXIT_SUCCESS;

err:
    return ret;
}
static int create_cloudmig_status(struct cloudmig_ctx *ctx,
                                  struct cldmig_entry *list)
{
    unsigned int    size = 0;
    int             ret = EXIT_FAILURE;
    dpl_status_t    dplret;
    dpl_vfile_t*    stfile = NULL;
    char            *buf = NULL;
    char            *curdata;

    cloudmig_log(DEBUG_LVL,
                 "[Creating Status]: Creating cloudmig general status file...\n"
                 );

    // First calc the final file's size (header + each chunk)
    size = sizeof(ctx->status.general.head);
    for (struct cldmig_entry *it=list; it != NULL; it = it->next)
    {
        size += sizeof(it->namlens);
        size += it->namlens.file;
        size += it->namlens.bucket;
    }

    // Then we can open and write it
    dplret = dpl_openwrite(ctx->src_ctx, ".cloudmig",
                           DPL_VFILE_FLAG_CREAT,
                           NULL, DPL_CANNED_ACL_PRIVATE, size, &stfile);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not open migration status file : %s.\n",
                 __FUNCTION__, dpl_status_str(dplret));
        goto end;
    }

    // create a buffer and fill it with the whole data.
    buf = calloc(size, sizeof(*buf));
    if (buf == NULL)
    {
        PRINTERR("%s: Could not write data into file : %s.\n",
                 __FUNCTION__, strerror(errno));
        goto end;
    }
    // first copy the header into it after setting network byte order.
    ctx->status.general.head.total_sz =
        htobe64(ctx->status.general.head.total_sz);
    ctx->status.general.head.nb_objects =
        htobe64(ctx->status.general.head.nb_objects);
    memcpy(buf, &ctx->status.general.head, sizeof(ctx->status.general.head));
    // Next is the data
    curdata = buf + sizeof(ctx->status.general.head);
    for (struct cldmig_entry *it=list; it != NULL; it = it->next)
    {
        it->namlens.file = htonl(it->namlens.file);
        it->namlens.bucket = htonl(it->namlens.bucket);
        memmove(curdata, &it->namlens.file,
                sizeof(it->namlens.file)); 
        curdata += sizeof(it->namlens.file);
        memmove(curdata, &it->namlens.bucket,
                sizeof(it->namlens.bucket)); 
        curdata += sizeof(it->namlens.bucket);
        strcpy(curdata, it->filename);
        curdata += ntohl(it->namlens.file);
        strcpy(curdata, it->bucket);
        curdata += ntohl(it->namlens.bucket);
    }

    // Then write it into the file.
    dplret = dpl_write(stfile, buf, size);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not write data to file : %s.\n",
                 __FUNCTION__, dpl_status_str(dplret));
        goto end;
    }

    ret = EXIT_SUCCESS;

end:
    if (stfile)
        dpl_close(stfile);
    return ret;
}

int create_status(struct cloudmig_ctx* ctx, dpl_vec_t* buckets)
{
    int                     ret;
    struct cldmig_entry     *st_list = NULL;
    char                    *status_filename = NULL;
    char                    *dest_bucket_name = NULL;

    cloudmig_log(INFO_LVL,
                 "[Creating Status]: Beginning creation of status...\n");

    // First, create the status bucket since it didn't exist.
    if ((ret = create_status_bucket(ctx)))
    {
        PRINTERR("%s: Could not create status bucket.\n", __FUNCTION__);
        goto err;
    }
    ctx->src_ctx->cur_bucket = ctx->status.bucket_name;

    /*
     * For each bucket, we create a file named after it in the
     * status bucket, and create the destination's bucket.
     */
    dpl_bucket_t**  cur_bucket = (dpl_bucket_t**)buckets->array;
    for (int i = 0; i < buckets->n_items; ++i, ++cur_bucket)
    {
        ret = create_status_file(ctx, (*cur_bucket)->name, &status_filename);
        if (ret != EXIT_SUCCESS)
        {
            PRINTERR(
"An Error happened while creating the status bucket and file.\n\
Please delete manually the bucket '%s' before restarting the tool...\n",
                     ctx->status.bucket_name);
            goto err;
        }

        dest_bucket_name = strdup((*cur_bucket)->name);
        if (dest_bucket_name == NULL)
        {
            PRINTERR("[Creating Status]: Could not create status for bucket %s:"
                     "Could not allocate memory\n", (*cur_bucket)->name);
            goto err;
        }

        ret = create_destination_bucket(ctx, &dest_bucket_name);
        if (ret != EXIT_SUCCESS)
            goto err; // Error already printed by callee
 
        ret = append_to_stlist(&st_list, &status_filename, &dest_bucket_name);
        if (ret != EXIT_SUCCESS)
            goto err; // Error already printed by callee
    }

    /*
     * Now, write the general status file (named ".cloudmig")
     */
    ret = create_cloudmig_status(ctx, st_list);
    if (ret != EXIT_SUCCESS)
        PRINTERR("%s: Could not create migration status file.\n",
                 __FUNCTION__);

err:
    if (status_filename)
        free(status_filename);

    while (st_list)
    {
        struct cldmig_entry *save = st_list;
        free(st_list->filename);
        free(st_list->bucket);
        st_list = st_list->next;
        free(save);
    }
    return ret;
}

int create_buckets_status(struct cloudmig_ctx* ctx)
{
    int                     ret;
    struct cldmig_entry     *st_list = NULL;
    char                    *status_filename = NULL;
    char                    *dest_bucket_name = NULL;

    cloudmig_log(INFO_LVL,
                 "[Creating Status]: Beginning creation of status...\n");

    // First, create the status bucket since it didn't exist.
    if ((ret = create_status_bucket(ctx)))
    {
        PRINTERR("%s: Could not create status bucket.\n", __FUNCTION__);
        goto err;
    }
    ctx->src_ctx->cur_bucket = ctx->status.bucket_name;

    /*
     * For each bucket, we create a file named after it in the
     * status bucket, and create the destination's bucket.
     *
     * Here, buckets is a string table ended by NULL pointer.
     */
    for (int i=0; gl_options->src_buckets[i]; ++i)
    {
        ret = create_status_file(ctx, gl_options->src_buckets[i],
                                 &status_filename);
        if (ret != EXIT_SUCCESS)
        {
            PRINTERR(
"An Error happened while creating the status bucket and file.\n\
Please delete manually the bucket '%s' before restarting the tool...\n",
                     ctx->status.bucket_name);
            goto err;
        }

        dest_bucket_name = strdup(gl_options->dst_buckets[i]);
        if (dest_bucket_name == NULL)
        {
            PRINTERR("[Creating Status]: Could not create status for bucket %s:"
                     "Could not allocate memory\n", gl_options->src_buckets[i]);
            goto err;
        }

        ret = create_destination_bucket(ctx, &dest_bucket_name);
        if (ret != EXIT_SUCCESS)
            goto err; // Error already printed by callee
 
        ret = append_to_stlist(&st_list, &status_filename, &dest_bucket_name);
        if (ret != EXIT_SUCCESS)
            goto err; // Error already printed by callee
    }

    /*
     * Now, write the general status file (named ".cloudmig")
     */
    ret = create_cloudmig_status(ctx, st_list);
    if (ret != EXIT_SUCCESS)
        PRINTERR("%s: Could not create migration status file.\n",
                 __FUNCTION__);

err:
    if (status_filename)
        free(status_filename);

    while (st_list)
    {
        struct cldmig_entry *save = st_list;
        free(st_list->filename);
        free(st_list->bucket);
        st_list = st_list->next;
        free(save);
    }
    return ret;
}



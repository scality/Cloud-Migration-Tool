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
#include "options.h"

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

    cloudmig_log(INFO_LVL, "[Loading Status]: Starting status loading...\n");

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
        if (gl_options->src_buckets)
            ret = create_buckets_status(ctx);
        else
            ret = create_status(ctx, src_buckets);
        if (ret == EXIT_FAILURE)
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
    if (resuming == 0
        || gl_options->flags & RESUME_MIGRATION)
    {
        if (status_retrieve_states(ctx))
            goto err;
    }
    else
    {
        PRINTERR("[Loading Status]: Could not create status:"
                 "Ongoing migration for this configuration.\n"
                 "Please use --force-resume option\n",  0);
        goto err;
    }

    cloudmig_log(INFO_LVL, "[Loading Status]: Status loading"
                 " done with success.\n");
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
 * The function allocates and retrieves the content of a bucket state file
 * if it is to be read.
 * It also frees the content of the bucket state files it already went over.
 *
 * 
 */
int status_next_incomplete_entry(struct cloudmig_ctx* ctx,
                                 struct file_transfer_state* filestate)
{
    assert(ctx != NULL);
    assert(ctx->status.bucket_name != NULL);

    int                     ret = EXIT_FAILURE;
    struct file_state_entry *fste;
    bool                    found = false;

    cloudmig_log(DEBUG_LVL,
                 "[Migrating]: Starting next incomplete entry search...\n");
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
        // TODO Lock bucket_state
        if (bucket_state->buf == NULL
            && cloudmig_map_bucket_state(ctx, bucket_state) == EXIT_FAILURE)
            goto err;
        // Increase the count to prevent it being freed after each file.
        // To be freed where no more file to transfer from it too.
        bucket_state->use_count += 1;

        // loop on the bucket state for each entry, until the end.
        while (bucket_state->next_entry_off < bucket_state->size)
        {
            fste = (void*)(bucket_state->buf + bucket_state->next_entry_off);
            /*
             * Check if this file has yet to be transfered
             *
             * Here we have to permanently use ntohl over fste's values,
             * since the int32_t are stored in network byte order.
             *
             * Moreover, since the directories have a size of 0, we have to
             * store the transfered file's size +1, in order to identify
             * finished transfers and unfinished ones. Thus, the comparison to
             * know whether a file is transfered or not is offset <= size.
             */
            if (ntohl(fste->offset) <= ntohl(fste->size))
            {
                found = true; // set the find flag
                // First, fill the fste struct...
                filestate->fixed.size = ntohl(fste->size);
                filestate->fixed.offset = ntohl(fste->offset);
                filestate->fixed.namlen = ntohl(fste->namlen);
                // Next, save the data allowing to find back this entry
                filestate->state_idx = i;
                filestate->offset = bucket_state->next_entry_off;

                // Find the ".cloudmig" in order to isolate the bucket's name
                // Not checked since the filename should always be a .cloudmig
                char* buckname_end = strrchr(bucket_state->filename, '.');
                // compute the whole filename in the form  srcbucket:filename
                if (asprintf(&filestate->name, "%.*s:/%s",
                             (int)(buckname_end - bucket_state->filename),
                             bucket_state->filename, (char*)(fste+1)) == -1)
                {
                    filestate->name = NULL;
                    PRINTERR("%s: could not compute src file name : %s",
                             __FUNCTION__, strerror(errno));
                    goto err;
                }

                // compute the whole filename in the form  dstbucket:filename
                if (asprintf(&filestate->dst, "%s:/%s",
                             bucket_state->dest_bucket, (char*)(fste+1)) == -1)
                {
                    filestate->dst = NULL;
                    PRINTERR("%s: could not compute dest file name : %s",
                             __FUNCTION__, strerror(errno));
                    goto err;
                }

                cloudmig_log(WARN_LVL,
                "[Migrating]: Found an incompletely transfered file :"
                " '%s' to '%s'...\n", filestate->name, filestate->dst);

                // For the threading :
                // Reference counter over the use of the buffer, in order
                // to avoid freeing it when the bucket is not fully transferred
                bucket_state->use_count += 1;

                // Advance for the next search
                bucket_state->next_entry_off +=
                    sizeof(*fste) + ntohl(fste->namlen);
                break ;
            }
            // If we do not break we need to advance in the file.
            bucket_state->next_entry_off += sizeof(*fste) + ntohl(fste->namlen);
        }

        if (found == true)
        {
            // TODO Unlock bucket_state (locked at loop's start)
            break ;
        }

        // Free only if no transfer reference the buffer.
        // Otherwise it will be done by the update status function
        bucket_state->use_count -= 1;
        if (bucket_state->use_count == 0)
        {
            free(bucket_state->buf);
            bucket_state->buf = NULL;
        }
        // TODO Unlock bucket_state (locked at loop's start)
        ctx->status.cur_state = i; // update the current state file index.
    }

    ret = EXIT_SUCCESS;
    if (!found)
    {
        cloudmig_log(DEBUG_LVL,
                     "[Migrating]: No more file to start transfering.\n");
        ret = ENODATA;
    }

    return ret;

err:
    if(filestate->name)
    {
        free(filestate->name);
        filestate->name = NULL;
    }
    if (filestate->dst)
    {
        free(filestate->dst);
        filestate->dst = NULL;
    }
    filestate->fixed.size = 0;
    filestate->fixed.offset = 0;
    filestate->fixed.namlen = 0;

    return ret;
}

/*
 * Function called when a file has been transfered successfully.
 * It updates the status of the matching bucket as well as the general
 * status.
 */
int		status_update_entry(struct cloudmig_ctx *ctx,
                            struct file_transfer_state *fst,
                            int32_t done_offset)
{
    int             ret = EXIT_FAILURE;
    dpl_status_t    dplret;
    dpl_vfile_t     *hfile = NULL;
    dpl_vfile_t     *hfile_genst = NULL;
    char            *ctx_bck = ctx->src_ctx->cur_bucket;
    ctx->src_ctx->cur_bucket = ctx->status.bucket_name;
    struct transfer_state   *bst = &ctx->status.bucket_states[fst->state_idx];

    cloudmig_log(INFO_LVL,
        "[Updating status]: File %s done up to %li/%li bytes.\n",
        fst->name, done_offset, fst->fixed.size);
    
    if (bst->buf == NULL)
    {
        PRINTERR("%s: Called for a freed bucket_state !\n", __FUNCTION__);
        goto end;
    }

    // TODO Lock bst
    /*
     * Since the directories have a size of 0, we have to
     * store the transfered file's size +1, in order to identify
     * finished transfers and unfinished ones.
     */
    ((struct file_state_entry*)(bst->buf + fst->offset))->offset =
        htonl(done_offset + 1);
    // TODO Unlock bst

    dplret = dpl_openwrite(ctx->src_ctx,
                           bst->filename,
                           DPL_VFILE_FLAG_MD5,
                           NULL, // metadata
                           DPL_CANNED_ACL_PRIVATE,
                           bst->size,
                           &hfile);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not open status file %s for updating.\n",
                 __FUNCTION__);
        goto end;
    }

    dplret = dpl_write(hfile, bst->buf, bst->size);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not write buffer to status file %s for updating.\n",
                 __FUNCTION__, bst->filename);
        goto end;
    }
    dpl_close(hfile);
    hfile = NULL;

    cloudmig_log(DEBUG_LVL, "[Updating status]: File %s updated.\n", fst->name);

    // TODO Lock bst
    bst->use_count -= 1;
    if (bst->use_count == 0)
    {
        free(bst->buf);
        bst->buf = NULL;
    }
    // TODO Unlock bst

    /*
     * If the file is fully transfered : update the object nb, not the size. 
     * It should have already been done in the transfer callback
     * */
    // TODO Lock general status
    if (done_offset == fst->fixed.size)
        ctx->status.general.head.done_objects += 1;
    ((struct cldmig_state_header*)ctx->status.general.buf)->done_sz =
        htobe64(ctx->status.general.head.done_sz);
    ((struct cldmig_state_header*)ctx->status.general.buf)->done_objects =
        htobe64(ctx->status.general.head.done_objects);
    dplret = dpl_openwrite(ctx->src_ctx,
                           ".cloudmig",
                           DPL_VFILE_FLAG_MD5,
                           NULL,
                           DPL_CANNED_ACL_PRIVATE,
                           ctx->status.general.size,
                           &hfile_genst);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not open general status file for update.\n",
                 __FUNCTION__);
        goto end;
    }

    dplret = dpl_write(hfile_genst,
                       ctx->status.general.buf, ctx->status.general.size);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not update general status file.\n", __FUNCTION__);
        goto end;
    }
    // TODO Unlock general status

end:
    if (hfile)
        dpl_close(hfile);
    if (hfile_genst)
        dpl_close(hfile_genst);
    ctx->src_ctx->cur_bucket = ctx_bck;
    return ret; 
}

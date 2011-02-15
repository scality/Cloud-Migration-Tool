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
//
#include <errno.h>
#include <string.h>

#include "cloudmig.h"

enum FileType
{
    TYPE_DIR,
    TYPE_FILE
};

/*
 * This callback receives the data read from a source,
 * and writes it into the destination file.
 */
static dpl_status_t
transfer_data_chunk()
{
    // XXX TODO FIXME
    return DPL_FAILURE;
}

/*
 * This function initiates and launches a file transfer :
 * it creates/opens the two files to be read and written,
 * and starts the transfer with a reading callback that will
 * write the data read into the file that is to be written.
 */
static int
transfer_file(struct cloudmig_ctx* ctx, struct file_transfer_state* filestate)
{
    int         ret = EXIT_FAILURE;

    // XXX TODO FIXME
    (void)ctx;
    (void)filestate;
    goto err;
    (void)transfer_data_chunk();

    ret = EXIT_SUCCESS;

err:

    return ret;
}

static int
create_directory(struct cloudmig_ctx* ctx,
                 struct file_transfer_state* filestate)
{ 
    //XXX TODO FIXME
    (void)ctx;
    (void)filestate;
    return EXIT_FAILURE;
}

/*
 * Returns a value allowing to identify whether the file is
 * a directory or a standard file
 */
static enum FileType
get_migrating_file_type(struct file_transfer_state* filestate)
{
    if (filestate->name[strlen(filestate->name) - 1] == '/')
        return TYPE_DIR;
    return TYPE_FILE;
}


/*
 * Main migration loop :
 *
 * Loops on every entry and starts the transfer of each.
 */
static int
migrate_loop(struct cloudmig_ctx* ctx)
{
    int                         ret = EXIT_FAILURE;
    struct file_transfer_state  cur_filestate;

    while ((ret = status_next_incomplete_entry(ctx, &cur_filestate))
           == EXIT_SUCCESS)
    {
        switch (get_migrating_file_type(&cur_filestate))
        {
        case TYPE_DIR:
            create_directory(ctx, &cur_filestate);
            break ;
        case TYPE_FILE:
            transfer_file(ctx, &cur_filestate);
            break ;
        default:
            break ;
        }
        //status_update_entry(ctx, &cur_filestate);
    }
    return (ret);
}


/*
 * First thing do to when starting the migration :
 *
 * Check the destination buckets exist, or create them if not.
 */
static int
create_dest_buckets(struct cloudmig_ctx* ctx)
{
    int             ret = EXIT_FAILURE;
    dpl_status_t    dplret = DPL_FAILURE;
    char            *name;
    dpl_vec_t       *buckets;

    dplret = dpl_list_all_my_buckets(ctx->dest_ctx, &buckets);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not list destination's buckets : %s\n",
                 __FUNCTION__, dpl_status_str(dplret));
        goto err;
    }

    dpl_bucket_t** curbck = (dpl_bucket_t**)buckets->array;
    for (int i = ctx->status.nb_states; i < ctx->status.nb_states; ++i)
    {
        int n;
        for  (n = 0; n < buckets->n_items; ++i)
        {
            if (strncmp(curbck[n]->name,
                        ctx->status.bucket_states[i].filename,
                        strlen(curbck[n]->name)) == 0)
                break ;
        }
        if (n != buckets->n_items) // means the bucket already exists
            continue ;

        name = strdup(ctx->status.bucket_states[i].filename);
        if (name == NULL)
        {
            PRINTERR("%s: Could not create every destination bucket: %s\n",
                     __FUNCTION__, strerror(errno));
            goto err;
        }
        char *ext = strrchr(name, '.');
        if (ext == NULL || strcmp(ext, ".cloudmig") != 0)
            goto next;


        /*
         * Here we want to create a bucket with the same attributes as the
         * source bucket. (acl and location constraints)
         *
         *  - Get the location constraint
         *  - Get the acl
         *  - Create the new bucket with appropriate informations.
         */
        // TODO FIXME XXX

next:
        free(name);
        name = NULL;
    }
err:
    if (buckets != NULL)
        dpl_vec_buckets_free(buckets);

    return ret;
}


/*
 * Main migration function.
 *
 * It manages every step of the migration, and the deletion of old objects
 * if the migration was a success.
 */
int
migrate(struct cloudmig_ctx* ctx)
{
    int                         ret = EXIT_FAILURE;

    /*
     * Since th S3 api does not allow an infinite number of buckets,
     * we can think ahead of time and create all the buckets that we'll
     * need.
     */
    ret = create_dest_buckets(ctx);


    ret = migrate_loop(ctx);
    // Check if it was the end of the transfer by checking ret agains ENODATA
    if (ret == ENODATA)
    {
        // TODO FIXME XXX
        // Then we have to remove the data from the source
    }
    else
        goto err;

    ret = EXIT_SUCCESS;

err:

    return ret;
}


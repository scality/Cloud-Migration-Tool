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
#include <stdbool.h>

#include "cloudmig.h"

/*
 * This callback receives the data read from a source,
 * and writes it into the destination file.
 */
static dpl_status_t
transfer_data_chunk(void* dst_hfile,
                    char *buf, unsigned int len)
{
    cloudmig_log(DEBUG_LVL,
    "[Migrating]: vFile %p : Transfering data chunk of %u bytes.\n",
    dst_hfile, len);
    return dpl_write((dpl_vfile_t*)dst_hfile, buf, len);
}

/*
 * This function initiates and launches a file transfer :
 * it creates/opens the two files to be read and written,
 * and starts the transfer with a reading callback that will
 * write the data read into the file that is to be written.
 */
// TODO FIXME : Do it with the correct attributes
static int
transfer_file(struct cloudmig_ctx* ctx,
              struct file_transfer_state* filestate,
              char* srcbucket,
              char* dstbucket)
{
    int                     ret = EXIT_FAILURE;
    dpl_status_t            dplret;
    char*                   bucket_dstctx = ctx->dest_ctx->cur_bucket;
    char*                   bucket_srcctx = ctx->src_ctx->cur_bucket;
    dpl_vfile_t             *dst_hfile;

    cloudmig_log(INFO_LVL,
"[Migrating] : file (len=%i)''%s'' is a regular file : starting transfer...\n",
                filestate->fixed.namlen, filestate->name);

    ctx->dest_ctx->cur_bucket = dstbucket;
    ctx->src_ctx->cur_bucket = srcbucket;
    
    
    /*
     * First, open the destination file for writing.
     */
    dplret = dpl_openwrite(ctx->dest_ctx, filestate->name,
                           DPL_VFILE_FLAG_CREAT,
                           NULL, // metadata
                           DPL_CANNED_ACL_PRIVATE,
                           filestate->fixed.size,
                           &dst_hfile);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not open dest file %s in bucket %s : %s\n",
                 __FUNCTION__, filestate->name, dstbucket,
                 dpl_status_str(dplret));
        goto err;
    }

    /*
     * Then open the source file for reading, with a callback
     * that will transfer each data chunk.
     */
    dplret = dpl_openread(ctx->src_ctx, filestate->name,
                          DPL_VFILE_FLAG_MD5,
                          NULL, // condition
                          transfer_data_chunk, dst_hfile, // reading cb,data
                          NULL); // metadatap
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not open source file %s in bucket %s : %s\n",
                 __FUNCTION__, filestate->name, srcbucket,
                 dpl_status_str(dplret));
        goto err;
    }

    /*
     * And finally, close the destination file written...
     */
    dplret = dpl_close(dst_hfile);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not close destination file %s in bucket %s : %s\n",
                 __FUNCTION__, filestate->name, dstbucket,
                 dpl_status_str(dplret));
    }

    ret = EXIT_SUCCESS;

    cloudmig_log(INFO_LVL, "[Migrating] File '%s' transfered successfully !\n",
                 filestate->name);

err:
    ctx->dest_ctx->cur_bucket = bucket_dstctx;
    ctx->src_ctx->cur_bucket = bucket_srcctx;

    return ret;
}

static int
create_directory(struct cloudmig_ctx* ctx,
                 struct file_transfer_state* filestate,
                 char* srcbucket,
                 char* dstbucket)
{ 
    int             ret = EXIT_FAILURE;
    dpl_status_t    dplret = DPL_SUCCESS;
    char*           bck_srcctx = ctx->src_ctx->cur_bucket;
    char*           bck_dstctx = ctx->dest_ctx->cur_bucket;


    cloudmig_log(INFO_LVL,
                 "[Migrating] : file '%s' is a directory : creating...\n",
                 filestate->name);

    ctx->dest_ctx->cur_bucket = dstbucket;
    ctx->src_ctx->cur_bucket  = srcbucket;

    dplret = dpl_mkdir(ctx->dest_ctx, filestate->name);
    // TODO FIXME With correct attributes
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not create destination dir '%s' in bucket %s\n",
                 __FUNCTION__, filestate->name, dstbucket);
        goto end;
    }

    ret = EXIT_SUCCESS;

    cloudmig_log(INFO_LVL,
                 "[Migrating] : directory '%s' successfully created !\n",
                 filestate->name);

end:
    ctx->dest_ctx->cur_bucket = bck_dstctx;
    ctx->src_ctx->cur_bucket = bck_srcctx;

    return ret;
}

/*
 * Returns a value allowing to identify whether the file is
 * a directory or a standard file
 */
static dpl_ftype_t
get_migrating_file_type(struct file_transfer_state* filestate)
{
    if (filestate->name[strlen(filestate->name) - 1] == '/')
        return DPL_FTYPE_DIR;
    return DPL_FTYPE_REG;
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
    struct file_transfer_state  cur_filestate = {{0, 0, 0}, NULL, 0, 0};
    char*                       srcbucket = NULL;
    char*                       dstbucket = NULL;
    int                         failures;

    // The call allocates the buffer for the bucket, so we must free it
    // The same goes for the cur_filestate's name field.
try_next_file:
    while ((ret = status_next_incomplete_entry(ctx, &cur_filestate,
                                               &srcbucket, &dstbucket))
           == EXIT_SUCCESS)
    {
        cloudmig_log(DEBUG_LVL,
"[Migrating] : starting migration of file %s from bucket %s to bucket %s.\n",
                     cur_filestate.name, srcbucket, dstbucket);
        failures = 0;
retry:
        switch (get_migrating_file_type(&cur_filestate))
        {
        case DPL_FTYPE_DIR:
            ret = create_directory(ctx, &cur_filestate, srcbucket, dstbucket);
            if (ret != EXIT_SUCCESS)
            {
                if (++failures < 3)
                {
                    cloudmig_log(DEBUG_LVL,
                    "[Migrating] : failure, retrying migration of file %s.\n",
                    cur_filestate.name);
                    goto retry;
                }
                goto try_next_file;
            }
            break ;
        case DPL_FTYPE_REG:
            ret = transfer_file(ctx, &cur_filestate, srcbucket, dstbucket);
            if (ret != EXIT_SUCCESS)
            {
                if (++failures < 3)
                {
                    goto retry;
                }
                goto try_next_file;
            }
            break ;
        default:
            break ;
        }
        status_update_entry(ctx, &cur_filestate,
                            srcbucket, cur_filestate.fixed.size);

        cloudmig_log(INFO_LVL,
        "[Migrating] : file %s from bucket %s migrated to dest bucket %s.\n",
                     cur_filestate.name, srcbucket, dstbucket);
        free(srcbucket);
        srcbucket = NULL;
        dstbucket = NULL; // this one is not allocated for us (Copied pointer).

        free(cur_filestate.name);
        cur_filestate.name = NULL;
    }

    if (srcbucket)
        free(srcbucket);
    if (cur_filestate.name)
        free(cur_filestate.name);

    return (ret);
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

    cloudmig_log(DEBUG_LVL, "Starting migration...\n");
    /*
     * Since th S3 api does not allow an infinite number of buckets,
     * we can think ahead of time and create all the buckets that we'll
     * need.
     */

    ret = migrate_loop(ctx);
    // Check if it was the end of the transfer by checking ret agains ENODATA
    if (ret == ENODATA)
    {
        cloudmig_log(DEBUG_LVL, "Migration finished with success !\n");
        // TODO FIXME XXX
        // Then we have to remove the data from the source
        delete_source(ctx);
    }
    else
    {
        PRINTERR("An error occured during the migration."
                 " At least one file could not be transfered\n", 0);
        goto err;
    }

    ret = EXIT_SUCCESS;

err:

    return ret;
}


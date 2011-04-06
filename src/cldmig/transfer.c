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
#include <sys/time.h>

#include "cloudmig.h"

struct data_transfer
{
    dpl_vfile_t             *hfile;
    struct cloudmig_ctx     *ctx;
};

/*
 * This callback receives the data read from a source,
 * and writes it into the destination file.
 */
static dpl_status_t
transfer_data_chunk(void* data, char *buf, unsigned int len)
{
    struct cldmig_transf*   e;
    struct timeval          tv;

    cloudmig_log(DEBUG_LVL,
    "[Migrating]: vFile %p : Transfering data chunk of %u bytes.\n",
    ((struct data_transfer*)data)->hfile, len);

    /*
     * Here, create an element for the byte rate computing list
     * and insert it in the right info list.
     */
    // XXX we should use the thread_idx here instead of hard-coded index
    ((struct data_transfer*)data)->ctx->tinfos[0].fdone += len;
    gettimeofday(&tv, NULL);
    e = new_transf_info(&tv, len);
    insert_in_list((struct cldmig_transf**)(
                    &((struct data_transfer*)data)->ctx->tinfos[0].infolist),
                   e);

    cloudmig_check_for_clients(((struct data_transfer*)data)->ctx);
    /* In any case, let's update a viewer-client if there's one */
    cloudmig_update_client(((struct data_transfer*)data)->ctx);


    return dpl_write(((struct data_transfer*)data)->hfile, buf, len);
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
    struct data_transfer    cb_data = { .hfile=NULL, .ctx=ctx };

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
                           &cb_data.hfile);
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
                          transfer_data_chunk, &cb_data, // reading cb,data
                          NULL); // metadatap
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not open source file %s in bucket %s : %s\n",
                 __FUNCTION__, filestate->name, srcbucket,
                 dpl_status_str(dplret));
        goto err;
    }

    /* And finally, close the destination file written... */
    dplret = dpl_close(cb_data.hfile);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not close destination file %s in bucket %s : %s\n",
                 __FUNCTION__, filestate->name, dstbucket,
                 dpl_status_str(dplret));
    }

    ret = EXIT_SUCCESS;

    /* 
     * Now update the transfered size in the general status
     * We can only do this here since in a multi-threaded context, we would
     * not be able to ensure the transfered size is rolled-back before a status
     * update in case of failure
     */
    // TODO Lock general status
    ctx->status.general.head.done_sz += filestate->fixed.size;
    // TODO Unlock general status

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

    /* 
     * FIXME : WORKAROUND : replace the last slash by a nul char
     * Since the dpl_mkdir function seems to fail when the last char is a slash
     */
    filestate->name[strlen(filestate->name) - 1] = 0;
    dplret = dpl_mkdir(ctx->dest_ctx, filestate->name);
    filestate->name[strlen(filestate->name)] = '/';
    // TODO FIXME With correct attributes
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not create destination dir '%s' in bucket %s: %s\n",
                 __FUNCTION__, filestate->name, dstbucket,
                 dpl_status_str(dplret));
        goto end;
    }

    ret = EXIT_SUCCESS;

    /* No general status size to update, since directories are empty files */

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

static int
migrate_object(struct cloudmig_ctx* ctx,
              struct file_transfer_state* filestate,
              char* srcbucket,
              char* dstbucket)
{
    int     failures = 0;
    int     ret = EXIT_FAILURE;

    cloudmig_log(DEBUG_LVL,
"[Migrating] : starting migration of file %s from bucket %s to bucket %s.\n",
                 filestate->name, srcbucket, dstbucket);

    /*
     * First init the thread's internal data
     */
    // XXX Lock it
    ctx->tinfos[0].fsize = filestate->fixed.size;
    ctx->tinfos[0].fdone = filestate->fixed.offset;
    ctx->tinfos[0].fnamlen = filestate->fixed.namlen;
    ctx->tinfos[0].fname = filestate->name;
    // XXX Unlock it
retry:
    switch (get_migrating_file_type(filestate))
    {
    case DPL_FTYPE_DIR:
        ret = create_directory(ctx, filestate, srcbucket, dstbucket);
        if (ret != EXIT_SUCCESS)
        {
            if (++failures < 3)
            {
                cloudmig_log(ERR_LVL,
                    "[Migrating] : failure, retrying migration of file %s.\n",
                             filestate->name);
                goto retry;
            }
            cloudmig_log(ERR_LVL,
                "[Migrating] : Could not migrate file %s...\n",
                filestate->name);
            goto ret;
        }
        break ;
    case DPL_FTYPE_REG:
        ret = transfer_file(ctx, filestate, srcbucket, dstbucket);
        if (ret != EXIT_SUCCESS)
        {
            if (++failures < 3)
            {
                cloudmig_log(ERR_LVL,
                    "[Migrating] : failure, retrying migration of file %s.\n",
                             filestate->name);
                goto retry;
            }
            cloudmig_log(ERR_LVL,
                "[Migrating] : Could not migrate file %s...\n",
                filestate->name);
            goto ret;
        }
        break ;
    default:
        PRINTERR("%s: File %s has no type attributed ? not transfered...\n",
                 __FUNCTION__, filestate->name);
        break ;
    }
    status_update_entry(ctx, filestate, srcbucket, filestate->fixed.size);

    cloudmig_log(INFO_LVL,
    "[Migrating] : file %s from bucket %s migrated to dest bucket %s.\n",
                 filestate->name, srcbucket, dstbucket);

ret:
    /*
     * Cleat the thread's internal data
     */
    // XXX Lock it
    ctx->tinfos[0].fsize = 0;
    ctx->tinfos[0].fdone = 0;
    ctx->tinfos[0].fnamlen = 0;
    ctx->tinfos[0].fname = NULL;
    clear_list((struct cldmig_transf**)(&(ctx->tinfos[0].infolist)));
    // XXX Unlock it
    return (failures < 3);
}


/*
 * Main migration loop :
 *
 * Loops on every entry and starts the transfer of each.
 */
static int
migrate_loop(struct cloudmig_ctx* ctx)
{
    unsigned int                ret = EXIT_FAILURE;
    struct file_transfer_state  cur_filestate = {{0, 0, 0}, NULL, 0, 0};
    char*                       srcbucket = NULL;
    char*                       dstbucket = NULL;
    size_t                      nbfailures = 0;

    // The call allocates the buffer for the bucket, so we must free it
    // The same goes for the cur_filestate's name field.
    while ((ret = status_next_incomplete_entry(ctx, &cur_filestate,
                                               &srcbucket, &dstbucket))
           == EXIT_SUCCESS)
    {
        if (migrate_object(ctx, &cur_filestate, srcbucket, dstbucket))
            ++nbfailures;

        // Clean up datas...
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

    return (ret == ENODATA ? nbfailures : ret);
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
    // Check if it was the end of the transfer by checking ret against 0
    if (ret == 0) // 0 == number of failures that occured.
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


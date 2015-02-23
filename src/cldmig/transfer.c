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
#include "options.h"

#include "status_store.h"
#include "status_digest.h"

static int
migrate_with_retries(struct cloudmig_ctx *ctx,
                     struct file_transfer_state *filestate,
                     int (*migfunc)(struct cloudmig_ctx*, struct file_transfer_state*),
                     int n_attempts)
{
    int ret;
    int failures = 0;

retry:
    ret = migfunc(ctx, filestate);
    if (ret != EXIT_SUCCESS)
    {
        if (++failures < n_attempts)
        {
            cloudmig_log(ERR_LVL,
                         "[Migrating] : failure, retrying migration of file %s\n",
                         filestate->obj_path);
            goto retry;
        }
        cloudmig_log(ERR_LVL,
                     "[Migrating] : Could not migrate file %s\n",
                     filestate->obj_path);
    }

    return ret;
}

static int
migrate_object(struct cloudmig_ctx* ctx,
               struct file_transfer_state* filestate)
{
    int             failures = 0;
    int             ret = EXIT_FAILURE;
    int             (*migfunc)(struct cloudmig_ctx*, struct file_transfer_state*) = NULL;

    cloudmig_log(DEBUG_LVL, "[Migrating] : starting migration of file %s\n",
                 filestate->obj_path);

    /*
     * First init the thread's internal data
     */
    // XXX Lock it
    ctx->tinfos[0].fsize = filestate->fixed.size;
    ctx->tinfos[0].fdone = filestate->fixed.offset;
    ctx->tinfos[0].fpath = filestate->obj_path;
    // XXX Unlock it

    switch (filestate->fixed.type)
    {
    case DPL_FTYPE_DIR:
        migfunc = &create_directory;
        break ;
    case DPL_FTYPE_SYMLINK:
        migfunc = &create_symlink;
        break ;
    case DPL_FTYPE_REG:
    default:
        migfunc = &transfer_file;
        break ;
    }
    ret = migrate_with_retries(ctx, filestate, migfunc, 3);
    if (ret != EXIT_SUCCESS)
        goto ret;

    status_store_entry_complete(ctx, filestate);

    cloudmig_log(INFO_LVL,
    "[Migrating] : file %s migrated.\n", filestate->obj_path);

ret:
    /*
     * Cleat the thread's internal data
     */
    // XXX Lock it
    ctx->tinfos[0].fsize = 0;
    ctx->tinfos[0].fdone = 0;
    ctx->tinfos[0].fpath = NULL;
    clear_list(&(ctx->tinfos[0].infolist));
    // XXX Unlock it
    return (failures == 3);
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
    struct file_transfer_state  cur_filestate = CLOUDMIG_FILESTATE_INITIALIZER;
    size_t                      nbfailures = 0;

    // The call allocates the buffer for the bucket, so we must free it
    // The same goes for the cur_filestate's name field.
    while ((ret = status_store_next_incomplete_entry(ctx, &cur_filestate)) == 1)
    {
        if (migrate_object(ctx, &cur_filestate))
            ++nbfailures;

        status_store_release_entry(&cur_filestate);
    }

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

    // In any case, attempt to update the status digest before anything else.
    (void)status_digest_upload(ctx->status->digest);

    // Check if it was the end of the transfer by checking ret against 0
    if (ret == 0) // 0 == number of failures that occured.
    {
        cloudmig_log(INFO_LVL, "Migration finished with success !\n");
        if (ctx->tinfos[0].config_flags & DELETE_SOURCE_DATA)
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


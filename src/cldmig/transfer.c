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
              struct file_transfer_state* filestate)
{
    int     failures = 0;
    int     ret = EXIT_FAILURE;

    cloudmig_log(DEBUG_LVL,
                 "[Migrating] : starting migration of file %s to %s.\n",
                 filestate->name, filestate->dst);

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
        ret = create_directory(ctx, filestate);
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
        ret = transfer_file(ctx, filestate);
        if (ret != EXIT_SUCCESS)
        {
            if (++failures < 3)
            {
                cloudmig_log(WARN_LVL,
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
    status_update_entry(ctx, filestate, filestate->fixed.size);

    cloudmig_log(INFO_LVL,
    "[Migrating] : file %s migrated to %s.\n",
                 filestate->name, filestate->dst);

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
    struct file_transfer_state  cur_filestate = {{0, 0, 0}, NULL, NULL, 0, 0};
    size_t                      nbfailures = 0;

    // The call allocates the buffer for the bucket, so we must free it
    // The same goes for the cur_filestate's name field.
    while ((ret = status_next_incomplete_entry(ctx, &cur_filestate))
           == EXIT_SUCCESS)
    {
        if (migrate_object(ctx, &cur_filestate))
            ++nbfailures;

        // Clean up datas...
        free(cur_filestate.name);
        free(cur_filestate.dst);
        cur_filestate.name = NULL;
        cur_filestate.dst= NULL;
    }

    if (cur_filestate.name)
        free(cur_filestate.name);
    if (cur_filestate.dst)
        free(cur_filestate.dst);

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
        cloudmig_log(INFO_LVL, "Migration finished with success !\n");
        if (gl_options->flags & DELETE_SOURCE_DATA)
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


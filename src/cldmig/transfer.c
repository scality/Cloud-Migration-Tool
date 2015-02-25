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
#include "display.h"

static int
migrate_with_retries(struct cldmig_info *tinfo,
                     struct file_transfer_state *filestate,
                     int (*migfunc)(struct cldmig_info *, struct file_transfer_state*),
                     int n_attempts)
{
    int ret;
    int failures = 0;

retry:
    ret = migfunc(tinfo, filestate);
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
migrate_object(struct cldmig_info *tinfo,
               struct file_transfer_state* filestate)
{
    int             failures = 0;
    int             ret = EXIT_FAILURE;
    int             (*migfunc)(struct cldmig_info*, struct file_transfer_state*) = NULL;

    cloudmig_log(DEBUG_LVL, "[Migrating] : starting migration of file %s\n",
                 filestate->obj_path);

    /*
     * Set the thread's internal data to the current file
     * (will stay as is until next file is set)
     */
    {
        pthread_mutex_lock(&tinfo->lock);
        tinfo->fsize = filestate->fixed.size;
        tinfo->fdone = filestate->fixed.offset;
        tinfo->fpath = filestate->obj_path;
        pthread_mutex_unlock(&tinfo->lock);
    }

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
    ret = migrate_with_retries(tinfo, filestate, migfunc, 3);
    if (ret != EXIT_SUCCESS)
        goto ret;

    status_store_entry_complete(tinfo->ctx, filestate);
    display_trigger_update(tinfo->ctx->display);

    cloudmig_log(INFO_LVL,
    "[Migrating] : file %s migrated.\n", filestate->obj_path);

ret:

    return (failures == 3);
}


/*
 * Main migration loop :
 *
 * Loops on every entry and starts the transfer of each.
 */
static void*
migrate_worker_loop(struct cldmig_info *tinfo)
{
    int                         found = 0;
    struct file_transfer_state  cur_filestate = CLOUDMIG_FILESTATE_INITIALIZER;
    size_t                      nbfailures = 0;

    // The call allocates the buffer for the bucket, so we must free it
    // The same goes for the cur_filestate's name field.
    pthread_mutex_lock(&tinfo->lock);
    while (tinfo->stop == false
           && (found = status_store_next_incomplete_entry(tinfo->ctx, &cur_filestate)) == 1)
    {
        pthread_mutex_unlock(&tinfo->lock);
        if (migrate_object(tinfo, &cur_filestate))
            ++nbfailures;

        status_store_release_entry(&cur_filestate);
        pthread_mutex_lock(&tinfo->lock);
    }

    clear_list(&tinfo->infolist);

    // Reset thread info for viewer to see inactive thread.
    tinfo->fsize = 0;
    tinfo->fdone = 0;
    tinfo->fpath = NULL;

    pthread_mutex_unlock(&tinfo->lock);

    /*
     * Found will equal -1 only in case of fatal status error.
     * It shall equal either 1 on program interrupt, or 0 on migration end.
     */
    return found != -1 ? (void*)nbfailures : (void*)-1;
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
    int                         nb_failures = 0;
    int                         ret;

    cloudmig_log(DEBUG_LVL, "Starting migration...\n");

    for (int i=0; i < ctx->options.nb_threads; ++i)
    {
        ctx->tinfos[i].stop = false;
        if (pthread_create(&ctx->tinfos[i].thr, NULL,
                           (void*(*)(void*))migrate_worker_loop,
                           &ctx->tinfos[i]) == -1)
        {
            PRINTERR("Could not start worker thread %i/%i", i, ctx->options.nb_threads);
            nb_failures = 1;
            // Stop all the already-running threads before attempting to join
            migration_stop(ctx);
            break ;
        }
    }

    /*
     * Join all the threads, and cumulate their error counts
     */
    for (int i=0; i < ctx->options.nb_threads; i++)
    {
        int errcount;
        ret = pthread_join(ctx->tinfos[i].thr, (void**)&errcount);
        if (ret != 0)
            cloudmig_log(WARN_LVL, "Could not join thread %i: %s.\n", i, strerror(errno));
        else
            ret += errcount;
    }

    // In any case, attempt to update the status digest before doing anything else
    (void)status_digest_upload(ctx->status->digest);

    // Check if it was the end of the transfer by checking ret against 0
    if (nb_failures == 0) // 0 == number of failures that occured.
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

err:

    return nb_failures;
}

void
migration_stop(struct cloudmig_ctx *ctx)
{
    for (int i=0; i < ctx->options.nb_threads; i++)
    {
        if (ctx->tinfos[i].lock_inited == 0)
            continue ;

        pthread_mutex_lock(&ctx->tinfos[i].lock);
        ctx->tinfos[i].stop = true;
        pthread_mutex_unlock(&ctx->tinfos[i].lock);
    }
}

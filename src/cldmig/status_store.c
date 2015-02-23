// Copyright (c) 2015, David Pineau
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

#include <droplet.h>
#include <droplet/vfs.h>

#include "cloudmig.h"
#include "status_store.h"
#include "status_bucket.h"
#include "status_digest.h"
#include "utils.h"


static void
_status_lock(struct cloudmig_status *status)
{
    pthread_mutex_lock(&status->lock);
}

static void
_status_unlock(struct cloudmig_status *status)
{
    pthread_mutex_unlock(&status->lock);
}

/*
 * result string will be of the form: "cloudmig.srchostname.to.desthostname"
 *
 * If this is too long, then we fallback to "cloudmig.status"
 */
static char*
status_store_name(char *src_host, char *dst_host)
{
    char* ret = NULL;
    char* name = NULL;

    assert(src_host != NULL);
    assert(dst_host != NULL);

    if (asprintf(&name, "cloudmig.%s.to.%s", src_host, dst_host) <= 0)
    {
        PRINTERR("Could not allocate store name string.\n");
        goto end;
    }

    ret = name;
    name = NULL;

end:
    if (name)
        free(name);

    return ret;
}

static char*
status_store_path(struct cloudmig_status *status, char *storename)
{
    char* ret = NULL;
    char* path = NULL;

    if (asprintf(&path, "%s%s/%s",
                 status->path_is_bucket ? storename : "",
                 status->path_is_bucket ? ":" : "",
                 status->path_is_bucket ? "" : storename) <= 0)
    {
        PRINTERR("Could not allocate Status Store path.\n");
        goto end;
    }

    ret = path;
    path = NULL;

end:
    if (path)
        free(path);

    return ret;
}

/*
 * This function creates the status store using the computed store name.
 * It first tries to create a bucket, and if it is not supported by the
 * backend, it falls back to creating a directory.
 */
static int
status_store_create(struct cloudmig_ctx *ctx, char *storename)
{
    int             ret;
    int             do_mkdir = 0;
    dpl_status_t    dplret;

    cloudmig_log(INFO_LVL, "[Creating Status Store] "
                 " Status Store not found. Creating...\n");

    dplret = dpl_make_bucket(ctx->status_ctx,
                             storename,
                             DPL_LOCATION_CONSTRAINT_UNDEF,
                             DPL_CANNED_ACL_PRIVATE);
    if (dplret != DPL_SUCCESS)
    {
        if (dplret != DPL_ENOTSUPP)
        {
            PRINTERR("[Creating Status Store] "
                     "Could not create store(bucket): %s\n", dpl_status_str(dplret));
            ret = EXIT_FAILURE;
            goto err;
        }
        do_mkdir = 1;
    }

    if (do_mkdir)
    {
        dplret = dpl_mkdir(ctx->status_ctx, storename,
                           NULL /*MD*/, NULL/*sysmd*/);
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("[Creating Status Store] "
                     "Could not create store(directory): %s\n",
                     dpl_status_str(dplret));
            ret = EXIT_FAILURE;
            goto err;
        }
    }

    cloudmig_log(INFO_LVL, "[Creating Status Store] "
                 "Created successfully !\n");
    
    ctx->status->path_is_bucket = !do_mkdir;

    ret = EXIT_SUCCESS;

err:
    return ret;
}

/*
 * This function checks that the status store exists.
 * It first tries to list the buckets, and if it is not supported by the
 * backend, it falls back to listing the root directory.
 *
 * In those listing, it looks for an entry with the same name as the computed
 * status store name., and returns 1 if it was found, 0 otherwise.
 */
static int
status_store_exists(struct cloudmig_ctx *ctx, char *storename)
{
    int             found = 0;
    dpl_status_t    dplret;
    int             do_readdir = 0;
    dpl_vec_t       *src_buckets = NULL;
    void            *dir_hdl = NULL;

    dplret = dpl_list_all_my_buckets(ctx->status_ctx, &src_buckets);
    if (dplret != DPL_SUCCESS)
    {
        if (dplret != DPL_ENOTSUPP)
        {
            PRINTERR("[Loading Status/Exists] "
                     "Could not list status stores(listbuckets): %s\n",
                     dpl_status_str(dplret));
            goto err;
        }
        do_readdir = 1;
    }

    cloudmig_log(DEBUG_LVL, "[Loading Status/Exists] "
                 "Attempting to find status store within a %s.\n",
                 do_readdir ? "directory" : "bucket");

    if (!do_readdir)
    {
        for (int i = 0; i < src_buckets->n_items; ++i)
        {
            const char *bucketname = ((dpl_bucket_t*)(src_buckets->items[i]->ptr))->name;
            if (strcmp(bucketname, storename) == 0)
            {
                cloudmig_log(DEBUG_LVL, "[Loading Status/Exists] "
                             "Found status store (bucket=%s) on storage\n",
                             storename);
                found = 1;
                break ;
            }
        }
    }
    else // do readdir != 0
    {
        dplret = dpl_opendir(ctx->status_ctx, storename, &dir_hdl);
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("[Loading Status/Exists] "
                     "Could not list open status store path(opendir): %s\n",
                     dpl_status_str(dplret));
            goto err;
        }
        found = 1;
    }

    ctx->status->path_is_bucket = !do_readdir;

err:
    if (dir_hdl)
        dpl_closedir(dir_hdl);

    if (src_buckets)
        dpl_vec_buckets_free(src_buckets);

    return found;
}

static int
_buckets_autoexpand(struct cloudmig_status *status)
{
    int                     ret;
    struct bucket_status    **tmp = NULL;

    if (status->n_loaded < status->n_buckets)
    {
        ret = EXIT_SUCCESS;
        goto end;
    }

    tmp = realloc(status->buckets, sizeof(*status->buckets) * (status->n_buckets + 10));
    if (tmp == NULL)
    {
        ret = EXIT_FAILURE;
        goto end;
    }

    for (int i=status->n_buckets; i < status->n_buckets+10; ++i)
        tmp[i] = NULL;

    status->n_buckets += 10;
    status->buckets = tmp;
    tmp = NULL;

    ret = EXIT_SUCCESS;

end:
    if (tmp)
        free(tmp);

    return ret;
}

int
status_store_entry_update(struct cloudmig_ctx *ctx,
                          struct file_transfer_state *filestate,
                          uint64_t done_chunk_size)
{
    int ret;

    ret = status_bucket_entry_update(ctx->status_ctx, filestate);
    if (ret != EXIT_SUCCESS)
    {
        cloudmig_log(WARN_LVL, "[Migrating] Could not update "
                     "state of migration for object %s\n", filestate->obj_path);
        goto end;
    }

    status_digest_add(ctx->status->digest, DIGEST_DONE_BYTES, done_chunk_size);

end:
    return ret;
}

int
status_store_entry_complete(struct cloudmig_ctx *ctx,
                            struct file_transfer_state *filestate)
{
    int ret;

    ret = status_bucket_entry_complete(ctx->status_ctx, filestate);
    if (ret != EXIT_SUCCESS)
    {
        cloudmig_log(WARN_LVL, "[Migrating] Could not register "
                     "end of migration for object %s\n", filestate->obj_path);
        goto end;
    }

    status_digest_add(ctx->status->digest, DIGEST_DONE_OBJECTS, 1);

end:
    return ret;
}

/*
 * This function lists the status files on the status store, and updates
 * the store by adding bucket migrations status missing on the store, using
 * the configuration as a reference.
 *
 * On the way, it loads each configuration file, included those not asked
 * by the configuration, but that were present on the status storage.
 */
static int
status_store_do_load_update(struct cloudmig_ctx *ctx, int regen_digest)
{
    int             ret;
    int             config_found[ctx->options.n_buckets];
    void            *dir_hdl = NULL;
    dpl_dirent_t    dirent;
    dpl_status_t    dplret;
    uint64_t        addcount, addsize;

    cloudmig_log(INFO_LVL, "[Loading Status Store] "
                 "Loading and updating store...\n");

    memset(config_found, 0, sizeof(config_found));

    dplret = dpl_opendir(ctx->status_ctx, ctx->status->store_path, &dir_hdl);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Loading Status Store] "
                 "Could not list status stores(opendir): %s\n",
                 dpl_status_str(dplret));
        goto err;
    }

    while (!dpl_eof(dir_hdl))
    {
        dplret = dpl_readdir(dir_hdl, &dirent);
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("[Loading Status Store] "
                     "Could not list status stores (readdir): %s\n",
                     dplret);
            goto err;
        }

        cloudmig_log(DEBUG_LVL, "[Loading Status Store] "
                     "Browsing repo: entry=%s\n", dirent.name);

        for (int config_index=0; config_index < ctx->options.n_buckets; ++config_index)
        {
            cloudmig_log(DEBUG_LVL, "[Loading Status Store] "
                         "Browsing options: entry=%s\n", ctx->options.src_buckets[config_index]);
            if (dirent.type == DPL_FTYPE_REG
                && status_bucket_namecmp(dirent.name,
                                         ctx->options.src_buckets[config_index]) == 0)
            {
                cloudmig_log(DEBUG_LVL, "[Loading Status Store] "
                             "Found bucket status (bucket=%s) on storage\n",
                             ctx->status->store_path);
                config_found[config_index] = 1;
                break ;
            }
        }

        /*
         * Found or not, it's a bucket migration status that exists on the
         * status store, so we shall load it, and resume its migration,
         * even if the current migration configuration does not include it.
         *
         * By the way... Avoid trying to load the digest as a bucket status.
         */
        if (dirent.type == DPL_FTYPE_REG
            && strcmp(dirent.name, ".cloudmig") != 0)
        {
            ret = _buckets_autoexpand(ctx->status);
            if (ret != EXIT_SUCCESS)
                goto err;

            ctx->status->buckets[ctx->status->n_loaded]
                = status_bucket_load(ctx->status_ctx,
                                     ctx->status->store_path, dirent.name,
                                     &addcount, &addsize);
            if (ctx->status->buckets[ctx->status->n_loaded] == NULL)
            {
                PRINTERR("[Loading Status Store] Could not load status file %s.\n",
                         dirent.name);
                ret = EXIT_FAILURE;
                goto err;
            }
            if (regen_digest)
            {
                status_digest_add(ctx->status->digest, DIGEST_OBJECTS, addcount);
                status_digest_add(ctx->status->digest, DIGEST_BYTES, addsize);
            }
            ctx->status->n_loaded++;
        }
    }

    /*
     * Generate and upload every status migration file associated to the
     * configured bucket migrations that we could not find on the status store.
     */
    for (int bucket=0; bucket < ctx->options.n_buckets; ++bucket)
    {
        cloudmig_log(DEBUG_LVL, "[Loading Status Store] "
                     "Attempting to create one bucket status: %s -> loaded=%i\n",
                     ctx->options.src_buckets[bucket], config_found[bucket]);
        if (!config_found[bucket])
        {
            ret = _buckets_autoexpand(ctx->status);
            if (ret != EXIT_SUCCESS)
                goto err;

            addcount = addsize = 0;
            ctx->status->buckets[ctx->status->n_loaded]
                = status_bucket_create(ctx->status_ctx, ctx->src_ctx,
                                       ctx->status->store_path,
                                       ctx->options.src_buckets[bucket],
                                       ctx->options.dst_buckets[bucket],
                                       &addcount, &addsize);
            if (ctx->status->buckets[ctx->status->n_loaded] == NULL)
            {
                PRINTERR("[Loading Status Store] "
                         "Could not create status for source bucket %s.\n",
                         ctx->options.src_buckets[bucket]);
                ret = EXIT_FAILURE;
                goto err;
            }
            ctx->status->n_loaded++;

            status_digest_add(ctx->status->digest, DIGEST_OBJECTS, addcount);
            status_digest_add(ctx->status->digest, DIGEST_BYTES, addsize);
        }
    }

    cloudmig_log(INFO_LVL, "[Loading Status Store] "
                 "Status Store successfully Loaded !\n");

    ret = EXIT_SUCCESS;

err:
    if (dir_hdl)
        dpl_closedir(dir_hdl);

    return ret;
}

/*
 * Main status store loading function.
 *
 * It will take care of creating and loading any status file missing:
 * - General Status file
 * - Each Bucket Status file
 */
int status_store_load(struct cloudmig_ctx* ctx, char *src_host, char *dst_host)
{
    int             ret = EXIT_SUCCESS;
    int             regen_digest = 0;
    char            *storename = NULL;

    cloudmig_log(INFO_LVL, "[Loading Status] Starting status loading...\n");

    // Ensure the status store exists
    storename = status_store_name(src_host, dst_host);
    if (storename == NULL)
    {
        ret = EXIT_FAILURE;
        goto end;
    }

    if (!status_store_exists(ctx, storename)
        && status_store_create(ctx, storename) != EXIT_SUCCESS)
    {
        ret = EXIT_FAILURE;
        goto end;
    }

    ctx->status->store_path = status_store_path(ctx->status, storename);
    if (ctx->status->store_path == NULL)
    {
        ret = EXIT_FAILURE;
        goto end;
    }

    ctx->status->digest = status_digest_new(ctx->status_ctx,
                                            ctx->status->store_path,
                                            50 /*upload digest every 50 object*/);
    if (ctx->status->digest == NULL)
    {
        ret = EXIT_FAILURE;
        goto end;
    }

    ret = status_digest_download(ctx->status->digest, &regen_digest);
    if (ret != EXIT_SUCCESS)
    {
        ret = EXIT_FAILURE;
        goto end;
    }

    ret = status_store_do_load_update(ctx, regen_digest);
    if (ret != EXIT_SUCCESS)
    {
        ret = EXIT_FAILURE;
        goto end;
    }

    // Force an upload to ensure the status digest is up to date on the status store
    ret = status_digest_upload(ctx->status->digest);
    if (ret != EXIT_SUCCESS)
    {
        ret = EXIT_FAILURE;
        goto end;
    }

    cloudmig_log(INFO_LVL, "[Loading Status] Status loading"
                 " done with success.\n");

    ret = EXIT_SUCCESS;

end:
    if (ctx->status->store_path != NULL)
    {
        free(ctx->status->store_path);
        ctx->status->store_path = NULL;
    }
    if (storename)
        free(storename);

    return ret;
}

void
status_store_delete(struct cloudmig_ctx *ctx)
{
    int i;

    _status_lock(ctx->status);
    for (i=0; i < ctx->status->n_loaded; ++i)
    {
        status_bucket_delete(ctx->status_ctx, ctx->status->buckets[i]);
    }
    status_digest_delete(ctx->status_ctx, ctx->status->digest);

    if (ctx->status->path_is_bucket)
        delete_bucket(ctx->status_ctx, "Status Store", ctx->status->store_path);
    else
        delete_directory(ctx->status_ctx, "Status Store", ctx->status->store_path);
    _status_unlock(ctx->status);
}

int
status_store_next_incomplete_entry(struct cloudmig_ctx *ctx,
                                   struct file_transfer_state *filestate)
{
    int                     ret = 0;
    struct bucket_status    *bst = NULL;
    bool                    status_locked = false;

    _status_lock(ctx->status);
    status_locked = true;

    while (ctx->status->cur_bucket < ctx->status->n_loaded)
    {
        bst = ctx->status->buckets[ctx->status->cur_bucket];
        if (bst == NULL)
        {
            ret = -1;
            goto end;
        }

        ret = status_bucket_next_incomplete_entry(ctx->status_ctx, bst, filestate);
        if (ret == -1)
            goto end;
        else if (ret == 1) // found, stop looking.
            break ;

        ctx->status->cur_bucket++;
    }

end:
    if (status_locked)
        _status_unlock(ctx->status);

    return ret;
}

int
status_store_next_entry(struct cloudmig_ctx *ctx,
                        struct file_transfer_state *filestate)
{
    int                     ret = 0;
    struct bucket_status    *bst = NULL;
    bool                    status_locked = false;

    _status_lock(ctx->status);
    status_locked = true;

    while (ctx->status->cur_bucket < ctx->status->n_loaded)
    {
        bst = ctx->status->buckets[ctx->status->cur_bucket];
        if (bst == NULL)
        {
            ret = -1;
            goto end;
        }

        ret = status_bucket_next_entry(ctx->status_ctx, bst, filestate);
        if (ret == -1)
            goto end;
        else if (ret == 1) // found, stop looking.
            break ;

        bst = NULL;

        ctx->status->cur_bucket++;
    }

end:
    if (status_locked)
        _status_unlock(ctx->status);

    return ret;
}

void
status_store_release_entry(struct file_transfer_state *filestate)
{
    status_bucket_release_entry(filestate);
}

void
status_store_reset_iteration(struct cloudmig_ctx *ctx)
{
    _status_lock(ctx->status);

    ctx->status->cur_bucket = 0;
    
    for (int i = 0; i < ctx->status->n_loaded; ++i)
        status_bucket_reset_iteration(ctx->status->buckets[i]);

    _status_unlock(ctx->status);
}

struct cloudmig_status*
status_store_new()
{
    struct cloudmig_status *ret = NULL;
    struct cloudmig_status *status = NULL;

    status = calloc(1, sizeof(*status));
    if (status == NULL)
    {
        PRINTERR("[Allocating Status Store] Failed.\n");
        goto end;
    }

    if (pthread_mutex_init(&status->lock, NULL) == -1)
    {
        PRINTERR("[Allocating Status Store] Could not initialize mutex.\n");
        goto end;
    }
    status->lock_inited = 1;

    ret = status;
    status = NULL;

end:
    if (status)
        status_store_free(status);
    return ret;
}

void
status_store_free(struct cloudmig_status *status)
{

    if (status->store_path)
        free(status->store_path);

    if (status->digest)
        status_digest_free(status->digest);

    if (status->buckets)
    {
        for (int i=0; i < status->n_buckets; ++i)
        {
            if (status->buckets[i])
                status_bucket_free(status->buckets[i]);
        }
        free(status->buckets);
    }
    
    if (status->lock_inited)
        pthread_mutex_destroy(&status->lock);

    free(status);
}

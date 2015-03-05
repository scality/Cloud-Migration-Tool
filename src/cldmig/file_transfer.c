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
#include <sys/time.h>
#include <droplet.h>
#include <droplet/vfs.h>
#include <libgen.h>

#include "cloudmig.h"
#include "options.h"

#include "status_store.h"
#include "status_digest.h"

/*
 * This function creates an element for the byte rate computing list
 * and inserts it in the info list.
 */
static void
_add_transfer_info(struct cldmig_info *tinfo, size_t len)
{
    struct timeval          tv;
    struct cldmig_transf    *e = NULL;

    pthread_mutex_lock(&tinfo->lock);

    tinfo->fdone += len;
    gettimeofday(&tv, NULL);
    e = new_transf_info(&tv, len);
    if (e == NULL)
        PRINTERR("Could not update ETA block list, ETA might become erroneous");
    else
        insert_in_list(&tinfo->infolist, e);

    pthread_mutex_unlock(&tinfo->lock);
}


static int
create_parent_dirs(struct cloudmig_ctx *ctx,
                   char *path)
{
    char            *delim = NULL;
    int             ret;
    dpl_status_t    dplret = DPL_SUCCESS;
    dpl_dict_t      *md = NULL;

    cloudmig_log(DEBUG_LVL, "[Migrating] Creating parent directory of file %s\n", path);

    delim = strrchr(path, '/');
    if (delim == NULL)
    {
        ret = EXIT_SUCCESS;
        goto err;
    }
    // FIXME: Workaround: Replace current delimiter by a nul char since the
    // dpl_mkdir function seems to fail when the last char is a delimiter.
    cloudmig_log(WARN_LVL, "[Migrating] Creating parent directory of file %s\n",
                 path);
    *delim = 0;
    cloudmig_log(WARN_LVL, "[Migrating] Creating parent directory=%s\n",
                 path);

    dplret = dpl_getattr(ctx->dest_ctx, path, NULL /*mdp*/, NULL/*sysmd*/);
    if (dplret != DPL_SUCCESS)
    {
        if (dplret != DPL_ENOENT)
        {
            ret = EXIT_FAILURE;
            goto err;
        }

        // ENOENT, try to create parents then self.
        ret = create_parent_dirs(ctx, path);
        if (ret != EXIT_SUCCESS)
            goto err;

        dplret = dpl_getattr(ctx->src_ctx, path, &md, NULL/*sysmd*/);
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("[Migrating] Could not get source directory %s attributes: %s.\n",
                     path, dpl_status_str(dplret));
            ret = EXIT_FAILURE;
            goto err;
        }

        /*
         * In a Multi-threaded context, directory might have been created by a
         * concurrent thread
         * -> EEXIST is not an error.
         */
        dplret = dpl_mkdir(ctx->dest_ctx, path, md, NULL/*sysmd*/);
        if (dplret != DPL_SUCCESS && dplret != DPL_EEXIST)
        {
            PRINTERR("[Migrating] Creating parent directory %s: %s\n",
                     path, dpl_status_str(dplret));
            ret = EXIT_FAILURE;
            goto err;
        }

        cloudmig_log(DEBUG_LVL,
                     "[Migrating] Parent directories created with success !\n");
    }

    ret = EXIT_SUCCESS;

err:
    if (delim)
        *delim = '/';
    if (md)
        dpl_dict_free(md);

    return ret;
}

int
create_directory(struct cldmig_info *tinfo,
                 struct file_transfer_state *filestate)
{
    int                     ret;
    dpl_status_t            dplret;
    struct cloudmig_ctx     *ctx = tinfo->ctx;
    int                     pathlen = strlen(filestate->dst_path);
    char                    *delim = NULL;
    dpl_dict_t              *md = NULL;
    char                    *parentdir = NULL;

    cloudmig_log(DEBUG_LVL, "[Migrating] Directory %s (%s -> %s)\n",
                 filestate->obj_path, filestate->src_path, filestate->dst_path);

    if (ctx->options.flags & AUTO_CREATE_DIRS)
    {
        parentdir = strdup(filestate->dst_path);
        if (parentdir == NULL)
        {
            PRINTERR("[Migrating] Could not strdup path for parent directory creation.\n");
            ret = EXIT_FAILURE;
            goto err;
        }

        ret = create_parent_dirs(ctx, parentdir);
        if (ret != EXIT_SUCCESS)
        {
            PRINTERR("[Migrating] Could not create parent directories for %s\n",
                     filestate->dst_path);
            ret = EXIT_FAILURE;
            goto err;
        }
    }

    /*
     * FIXME
     * Workaround of a behavior from VFS API: Cannot mkdir with paths ending by
     * a delimiter, so we null the ending delimiter if any
     */
    if (pathlen > 1 && filestate->dst_path[pathlen - 1] == '/')
    {
        delim = &filestate->dst_path[pathlen];
        while (delim > filestate->dst_path+1 && *(delim-1) == '/')
            --delim;
        *delim = 0;
    }
    else
        delim = NULL;

    dplret = dpl_getattr(ctx->src_ctx, filestate->src_path, &md, NULL/*sysmd*/);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Migrating] Could not get source directory %s attributes: %s.\n",
                 filestate->src_path, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto err;
    }

    /*
     * In a multi-threaded context,
     * the directory might have already been created by another thread
     * -> EEXIST is not an error.
     */
    dplret = dpl_mkdir(ctx->dest_ctx, filestate->dst_path, md, NULL/*sysmd*/);
    if (dplret != DPL_SUCCESS && dplret != DPL_EEXIST)
    {
        PRINTERR("[Migrating] "
                 "Could not create directory %s : %s.\n",
                 filestate->dst_path, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto err;
    }

    // Update info list for viewer's ETA
    _add_transfer_info(tinfo, 0);

    ret = EXIT_SUCCESS;

err:
    if (parentdir)
        free(parentdir);
    if (delim)
        *delim = '/';
    if (md)
        dpl_dict_free(md);

    return ret;
}

int
create_symlink(struct cldmig_info *tinfo,
               struct file_transfer_state *filestate)
{
    int                     ret;
    dpl_status_t            dplret;
    struct cloudmig_ctx     *ctx = tinfo->ctx;
    char                    *link_target = NULL;
    char                    *parentdir = NULL;

    cloudmig_log(DEBUG_LVL, "[Migrating] Creating symlink %s\n",
                 filestate->obj_path);

    if (ctx->options.flags & AUTO_CREATE_DIRS)
    {
        parentdir = strdup(filestate->dst_path);
        if (parentdir == NULL)
        {
            PRINTERR("[Migrating] Could not strdup path for parent directory creation.\n");
            ret = EXIT_FAILURE;
            goto err;
        }

        ret = create_parent_dirs(ctx, parentdir);
        if (ret != EXIT_SUCCESS)
        {
            PRINTERR("[Migrating] Could not create directory %s\n",
                     filestate->dst_path);
            ret = EXIT_FAILURE;
            goto err;
        }
    }

    dplret = dpl_readlink(ctx->src_ctx, filestate->src_path, &link_target);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Migrating] "
                 "Could not read target of symlink %s : %s.\n",
                 filestate->src_path, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto err;
    }

    dplret = dpl_symlink(ctx->dest_ctx, link_target, filestate->dst_path);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Migrating] "
                 "Could not create symlink %s to file %s : %s\n",
                 filestate->dst_path, link_target, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto err;
    }

    // Update info list for viewer's ETA
    _add_transfer_info(tinfo, 0);

    status_digest_add(ctx->status->digest, DIGEST_DONE_BYTES, filestate->fixed.size);

    ret = EXIT_SUCCESS;

err:
    if (link_target)
        free(link_target);
    if (parentdir)
        free(parentdir);

    return ret;
}

/*
 * This callback receives the data read from a source,
 * and writes it into the destination file.
 */
static dpl_status_t
transfer_data_chunk(struct cldmig_info *tinfo,
                    struct file_transfer_state *filestate,
                    dpl_vfile_t *src, dpl_vfile_t *dst,
                    uint64_t *bytes_transferedp)
{
    dpl_status_t            ret = DPL_FAILURE;
    struct cloudmig_ctx     *ctx = tinfo->ctx;
    struct json_object      *rstatus = NULL;
    struct json_object      *wstatus = NULL;
    char                    *buffer = NULL;
    unsigned int            buflen = 0;

    cloudmig_log(DEBUG_LVL,
                 "[Migrating] %s : Transfering data chunk of %lu bytes.\n",
                 filestate->obj_path, ctx->options.block_size);

    ret = dpl_fstream_get(src, ctx->options.block_size,
                          &buffer, &buflen, &rstatus);
    if (ret != DPL_SUCCESS)
    {
        PRINTERR("Could not get next block from source file %s : %s.\n",
                 filestate->src_path, dpl_status_str(ret));
        ret = DPL_FAILURE;
        goto err;
    }

    ret = dpl_fstream_put(dst, buffer, buflen, &wstatus);
    if (ret != DPL_SUCCESS)
    {
        PRINTERR("Could not put next block to destination file %s : %s.\n",
                 filestate->dst_path, dpl_status_str(ret));
        ret = DPL_FAILURE;
        goto err;
    }

    // Update info list for viewer's ETA
    _add_transfer_info(tinfo, buflen);

    if (filestate->rstatus)
        json_object_put(filestate->rstatus);
    filestate->rstatus = rstatus;
    rstatus = NULL;

    if (filestate->wstatus)
        json_object_put(filestate->wstatus);
    filestate->wstatus = wstatus;
    wstatus = NULL;

    filestate->fixed.offset += buflen;

    *bytes_transferedp = buflen;

    ret = DPL_SUCCESS;

err:
    if (buffer)
        free(buffer);
    if (rstatus)
        json_object_put(rstatus);
    if (wstatus)
        json_object_put(wstatus);

    return ret;
}

int
transfer_chunked(struct cldmig_info *tinfo,
                 struct file_transfer_state *filestate)
{
    int                     ret = EXIT_FAILURE;
    dpl_status_t            dplret = DPL_FAILURE;
    struct cloudmig_ctx     *ctx = tinfo->ctx;
    dpl_vfile_t             *src = NULL;
    dpl_vfile_t             *dst = NULL;

    cloudmig_log(WARN_LVL, "Transfer Chunked of file %s\n", filestate->obj_path);
    /*
     * Open the source file for reading
     */
    dplret = dpl_open(ctx->src_ctx, filestate->src_path,
                      DPL_VFILE_FLAG_RDONLY|DPL_VFILE_FLAG_STREAM,
                      NULL /* opts */, NULL /* cond */,
                      NULL/* MD */, NULL /* sysmd */,
                      NULL /* query params */,
                      filestate->rstatus,
                      &src);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not open source file %s: %s\n",
                 __FUNCTION__, filestate->src_path, dpl_status_str(dplret));
        goto err;
    }

    /*
     * Open the destination file for writing
     */
    dplret = dpl_open(ctx->dest_ctx, filestate->dst_path,
                      DPL_VFILE_FLAG_CREAT|DPL_VFILE_FLAG_WRONLY|DPL_VFILE_FLAG_STREAM,
                      NULL /* opts */, NULL /* cond */,
                      NULL /*MD*/, NULL/*SYSMD*/,
                      NULL /* query params */,
                      filestate->wstatus,
                      &dst);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not open dest file %s: %s\n",
                 __FUNCTION__, filestate->dst_path, dpl_status_str(dplret));
        goto err;
    }

    /* Transfer the actual data */
    while (filestate->fixed.offset < filestate->fixed.size)
    {
        uint64_t    bytes_transfered;

        ret = transfer_data_chunk(tinfo, filestate, src, dst, &bytes_transfered);
        if (ret != EXIT_SUCCESS)
            goto err;

        ret = status_store_entry_update(ctx, filestate, bytes_transfered);
        if (ret != EXIT_SUCCESS)
            goto err;
    }

    /*
     * Flush the destination stream to ensure everything is written and comitted
     */
    dplret = dpl_fstream_flush(dst);
    if (DPL_SUCCESS != dplret)
    {
        PRINTERR("%s: Could not flush destination file %s: %s",
                __FUNCTION__, filestate->dst_path, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto err;
    }

    ret = EXIT_SUCCESS;

err:
    if (dst)
    {
        dplret = dpl_close(dst);
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("%s: Could not close destination file %s: %s\n",
                     __FUNCTION__, filestate->dst_path,
                     dpl_status_str(dplret));
        }
    }

    if (src)
    {
        dplret = dpl_close(src);
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("%s: Could not close source file %s: %s\n",
                     __FUNCTION__, filestate->src_path,
                     dpl_status_str(dplret));
        }
    }

    return ret;
}

/*
 * As this functions transfers a file whole, there is no need for intermediary
 * status updates, as the migrate_obj() caller completes an object's transfer.
 */
int
transfer_whole(struct cldmig_info *tinfo,
               struct file_transfer_state *filestate)
{
    int                     ret = EXIT_FAILURE;
    dpl_status_t            dplret = DPL_FAILURE;
    struct cloudmig_ctx     *ctx = tinfo->ctx;
    char                    *buffer = NULL;
    unsigned int            buflen = 0;
    dpl_dict_t              *metadata = NULL;
    dpl_sysmd_t             sysmd;

    memset(&sysmd, 0, sizeof(sysmd));

    dplret = dpl_fget(ctx->src_ctx, filestate->src_path, NULL, NULL, NULL,
                      &buffer, &buflen, &metadata, &sysmd);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Migrating] Could not fget source file %s: %s\n",
                 filestate->src_path, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto err;
    }

    dplret = dpl_fput(ctx->dest_ctx, filestate->dst_path, NULL, NULL, NULL,
                      metadata, &sysmd, buffer, buflen);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Migrating] Could not fput destination file %s: %s\n",
                 filestate->dst_path, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto err;
    }

    // Update info list for viewer's ETA
    _add_transfer_info(tinfo, buflen);

    status_digest_add(ctx->status->digest, DIGEST_DONE_BYTES, filestate->fixed.size);

    ret = EXIT_SUCCESS;

err:
    if (buffer)
        free(buffer);
    if (metadata)
        dpl_dict_free(metadata);

    return ret;
}

/*
 * This function initiates and launches a file transfer :
 * it creates/opens the two files to be read and written,
 * and starts the transfer with a reading callback that will
 * write the data read into the file that is to be written.
 */
int
transfer_file(struct cldmig_info* tinfo,
              struct file_transfer_state* filestate)
{
    int                     ret;
    char                    *parentdir = NULL;

    cloudmig_log(INFO_LVL,
    "[Migrating] : file '%s' is a regular file : starting transfer...\n",
    filestate->obj_path);


    if (tinfo->config_flags & AUTO_CREATE_DIRS)
    {
        parentdir = strdup(filestate->dst_path);
        if (parentdir == NULL)
        {
            PRINTERR("[Migrating] Could not strdup path for parent directory creation.\n");
            ret = EXIT_FAILURE;
            goto err;
        }

        if (create_parent_dirs(tinfo->ctx, parentdir) == EXIT_FAILURE)
        {
            PRINTERR("[Migrating] Could not create parent directories for file %s\n",
                     filestate->dst_path);
            ret = EXIT_FAILURE;
            goto err;
        }
    }

    ret = (filestate->fixed.size > tinfo->ctx->options.block_size) ?
            transfer_chunked(tinfo, filestate) : transfer_whole(tinfo, filestate);

    cloudmig_log(INFO_LVL, "[Migrating] File '%s' transfer %s !\n",
                 filestate->obj_path, ret == EXIT_SUCCESS ? "succeeded" : "failed");

err:
    if (parentdir)
        free(parentdir);

    return ret;
}

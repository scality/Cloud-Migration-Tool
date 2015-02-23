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

static int
create_parent_dirs(struct cloudmig_ctx *ctx,
                   struct file_transfer_state *filestate)
{
    char            *delim = NULL;
    int             ret;
    dpl_status_t    dplret = DPL_SUCCESS;
    dpl_dict_t      *md = NULL;

    cloudmig_log(DEBUG_LVL, "[Migrating] Creating parent directory of file %s\n",
                 filestate->obj_path);

    delim = strrchr(filestate->obj_path, '/');
    if (delim == NULL)
    {
        ret = EXIT_SUCCESS;
        goto end;
    }
    // FIXME: Workaround: Replace current delimiter by a nul char since the
    // dpl_mkdir function seems to fail when the last char is a delimiter.
    cloudmig_log(WARN_LVL, "[Migrating] Creating parent directory of file %s\n",
                 filestate->obj_path);
    *delim = 0;
    cloudmig_log(WARN_LVL, "[Migrating] Creating parent directory=%s\n",
                 filestate->obj_path);

    dplret = dpl_getattr(ctx->dest_ctx, filestate->obj_path, NULL /*mdp*/, NULL/*sysmd*/);
    if (dplret != DPL_SUCCESS)
    {
        if (dplret != DPL_ENOENT)
        {
            ret = EXIT_FAILURE;
            goto end;
        }

        // ENOENT, try to create parents then self.
        ret = create_parent_dirs(ctx, filestate);
        if (ret != EXIT_SUCCESS)
            goto end;

        dplret = dpl_getattr(ctx->src_ctx, filestate->obj_path, &md, NULL/*sysmd*/);
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("[Migrating] Could not get source directory %s attributes: %s.\n",
                     filestate->obj_path, dpl_status_str(dplret));
            ret = EXIT_FAILURE;
            goto end;
        }

        dplret = dpl_mkdir(ctx->dest_ctx, filestate->obj_path, md, NULL/*sysmd*/);
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("[Migrating] Creating parent directory %s: %s\n",
                     filestate->obj_path, dpl_status_str(dplret));
            ret = EXIT_FAILURE;
            goto end;
        }

        cloudmig_log(DEBUG_LVL,
                     "[Migrating] Parent directories created with success !\n");
    }

    ret = EXIT_SUCCESS;

end:
    if (delim)
        *delim = '/';
    if (md)
        dpl_dict_free(md);

    return ret;
}

int
create_directory(struct cloudmig_ctx *ctx,
                 struct file_transfer_state *filestate)
{
    int             ret;
    dpl_status_t    dplret;
    int             pathlen = strlen(filestate->obj_path);
    char            *delim = NULL;
    dpl_dict_t      *md = NULL;


    cloudmig_log(DEBUG_LVL, "[Migrating] Directory %s\n",
                 filestate->obj_path);

    if (ctx->options.flags & AUTO_CREATE_DIRS)
    {
        ret = create_parent_dirs(ctx, filestate);
        if (ret != EXIT_SUCCESS)
        {
            PRINTERR("[Creating Directory] Could not create directory %s\n",
                     filestate->obj_path);
            ret = EXIT_FAILURE;
            goto end;
        }
    }

    /*
     * FIXME
     * Workaround of a behavior from VFS API: Cannot mkdir with paths ending by
     * a delimiter, so we null the ending delimiter if any
     */
    if (pathlen > 1 && filestate->obj_path[pathlen - 1] == '/')
    {
        delim = &filestate->obj_path[pathlen];
        while (delim > filestate->obj_path+1 && *(delim-1) == '/')
            --delim;
        *delim = 0;
    }
    else
        delim = NULL;

    dplret = dpl_getattr(ctx->src_ctx, filestate->obj_path, &md, NULL/*sysmd*/);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Migrating] Could not get source directory %s attributes: %s.\n",
                 filestate->obj_path, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto end;
    }

    dplret = dpl_mkdir(ctx->dest_ctx, filestate->obj_path, md, NULL/*sysmd*/);
    if (dplret != DPL_SUCCESS && dplret != DPL_EEXIST)
    {
        PRINTERR("[Migrating] "
                 "Could not create directory %s : %s.\n",
                 filestate->obj_path, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto end;
    }

    ret = EXIT_SUCCESS;

end:
    if (delim)
        *delim = '/';
    if (md)
        dpl_dict_free(md);

    return ret;
}

int
create_symlink(struct cloudmig_ctx *ctx,
               struct file_transfer_state *filestate)
{
    int             ret;
    dpl_status_t    dplret;
    char            *link_target = NULL;
    char            *tmppath = NULL;
    char            *link_dir = NULL;
    char            *dstroot = NULL;
    char            *buckend = NULL;
    int             bucklen = 0;

    cloudmig_log(DEBUG_LVL, "[Migrating] Creating symlink %s\n",
                 filestate->obj_path);

    tmppath = strdup(filestate->obj_path);
    if (tmppath == NULL)
    {
        PRINTERR("[Migrating] "
                 "Could not dup the filepath. Out Of Memory.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    link_dir = dirname(tmppath);
    buckend = strchr(filestate->obj_path, ':');
    bucklen = 0;
    if (buckend)
        bucklen = (int)(intptr_t)(buckend - filestate->obj_path);
    dstroot = calloc(bucklen + 3 /* for the ending ":/" */, sizeof(*dstroot));
    if (dstroot == NULL)
    {
        PRINTERR("[Migrating] "
                 "Could not dup the filepath. Out Of Memory.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    ret = sprintf(dstroot, "%.*s:/", bucklen ? bucklen : 0, filestate->obj_path);
    if (ret < bucklen + 2)
    {
        PRINTERR("[Migrating] Could not compute the root path\n");
        ret = EXIT_FAILURE;
        goto end;
    }
    
    /*
     * We need to:
     *
     * Read source link
     *
     * chdir to link parent dir
     * Create dest link
     * chdir back to "root" dir
     */
    dplret = dpl_readlink(ctx->src_ctx, filestate->obj_path, &link_target);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Migrating] "
                 "Could not read target of symlink %s : %s.\n",
                 filestate->obj_path, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto end;
    }
    cloudmig_log(ERR_LVL, "[Migrating] Symlink -> %s -> %s\n", filestate->obj_path, link_target);

    dplret = dpl_chdir(ctx->dest_ctx, link_dir);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Migrating] "
                 "Could not chdir to %s : %s.\n",
                 link_dir, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto end;
    }

    dplret = dpl_symlink(ctx->dest_ctx, link_target, filestate->obj_path);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Migrating] "
                 "Could not create symlink %s to file %s : %s\n",
                 filestate->obj_path, link_target, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto end;
    }

    dplret = dpl_chdir(ctx->dest_ctx, dstroot);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Migrating] "
                 "Could not chdir to %s : %s.\n",
                 link_dir, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto end;
    }


    status_digest_add(ctx->status->digest, DIGEST_DONE_BYTES, filestate->fixed.size);

    ret = EXIT_SUCCESS;

end:
    if (link_target)
        free(link_target);
    if (tmppath)
        free(tmppath);
    if (dstroot)
        free(dstroot);

    return ret;
}

/*
 * This callback receives the data read from a source,
 * and writes it into the destination file.
 */
static dpl_status_t
transfer_data_chunk(struct cloudmig_ctx *ctx,
                    struct file_transfer_state *filestate,
                    dpl_vfile_t *src, dpl_vfile_t *dst,
                    uint64_t *bytes_transferedp)
{
    dpl_status_t            ret = DPL_FAILURE;
    struct json_object      *rstatus = NULL;
    struct json_object      *wstatus = NULL;
    char                    *buffer = NULL;
    unsigned int            buflen = 0;
    // For ETA data
    struct cldmig_transf    *e;
    struct timeval          tv;

    cloudmig_log(DEBUG_LVL,
                 "[Migrating] %s : Transfering data chunk of %lu bytes.\n",
                 filestate->obj_path, ctx->options.block_size);

    ret = dpl_fstream_get(src, ctx->options.block_size,
                          &buffer, &buflen, &rstatus);
    if (ret != DPL_SUCCESS)
    {
        PRINTERR("Could not get next block from source file %s : %s.\n",
                 filestate->obj_path, dpl_status_str(ret));
        ret = DPL_FAILURE;
        goto end;
    }

    ret = dpl_fstream_put(dst, buffer, buflen, &wstatus);
    if (ret != DPL_SUCCESS)
    {
        PRINTERR("Could not put next block to destination file %s : %s.\n",
                 filestate->obj_path, dpl_status_str(ret));
        ret = DPL_FAILURE;
        goto end;
    }

    /*
     * Here, create an element for the byte rate computing list
     * and insert it in the right info list.
     */
    // XXX we should use the thread_idx here instead of hard-coded index
    ctx->tinfos[0].fdone += buflen;
    gettimeofday(&tv, NULL);
    e = new_transf_info(&tv, buflen);
    if (e == NULL)
        PRINTERR("Could not update ETA block list, ETA might become erroneous");
    else
        insert_in_list(&ctx->tinfos[0].infolist, e);

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

end:
    if (buffer)
        free(buffer);
    if (rstatus)
        json_object_put(rstatus);
    if (wstatus)
        json_object_put(wstatus);

    return ret;
}

int
transfer_chunked(struct cloudmig_ctx *ctx,
                 struct file_transfer_state *filestate)
{
    int             ret = EXIT_FAILURE;
    dpl_status_t    dplret = DPL_FAILURE;
    dpl_vfile_t     *src = NULL;
    dpl_vfile_t     *dst = NULL;

    cloudmig_log(WARN_LVL, "Transfer Chunked of file %s\n", filestate->obj_path);
    /*
     * Open the source file for reading
     */
    dplret = dpl_open(ctx->src_ctx, filestate->obj_path,
                      DPL_VFILE_FLAG_RDONLY|DPL_VFILE_FLAG_STREAM,
                      NULL /* opts */, NULL /* cond */,
                      NULL/* MD */, NULL /* sysmd */,
                      NULL /* query params */,
                      filestate->rstatus,
                      &src);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not open source file %s: %s\n",
                 __FUNCTION__, filestate->obj_path, dpl_status_str(dplret));
        goto err;
    }

    /*
     * Open the destination file for writing
     */
    dplret = dpl_open(ctx->dest_ctx, filestate->obj_path,
                      DPL_VFILE_FLAG_CREAT|DPL_VFILE_FLAG_WRONLY|DPL_VFILE_FLAG_STREAM,
                      NULL /* opts */, NULL /* cond */,
                      NULL /*MD*/, NULL/*SYSMD*/,
                      NULL /* query params */,
                      filestate->wstatus,
                      &dst);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not open dest file %s: %s\n",
                 __FUNCTION__, filestate->obj_path, dpl_status_str(dplret));
        goto err;
    }

    /* Transfer the actual data */
    while (filestate->fixed.offset < filestate->fixed.size)
    {
        uint64_t    bytes_transfered;

        ret = transfer_data_chunk(ctx, filestate, src, dst, &bytes_transfered);
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
                __FUNCTION__, filestate->obj_path, dpl_status_str(dplret));
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
                     __FUNCTION__, filestate->obj_path,
                     dpl_status_str(dplret));
        }
    }

    if (src)
    {
        dplret = dpl_close(src);
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("%s: Could not close source file %s: %s\n",
                     __FUNCTION__, filestate->obj_path,
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
transfer_whole(struct cloudmig_ctx *ctx,
               struct file_transfer_state *filestate)
{
    int             ret = EXIT_FAILURE;
    dpl_status_t    dplret = DPL_FAILURE;
    char            *buffer = NULL;
    unsigned int    buflen = 0;
    dpl_dict_t      *metadata = NULL;
    dpl_sysmd_t     sysmd;

    memset(&sysmd, 0, sizeof(sysmd));

    dplret = dpl_fget(ctx->src_ctx, filestate->obj_path, NULL, NULL, NULL,
                      &buffer, &buflen, &metadata, &sysmd);
    if (dplret != DPL_SUCCESS)
    {
        ret = EXIT_FAILURE;
        goto end;
    }

    dplret = dpl_fput(ctx->dest_ctx, filestate->obj_path, NULL, NULL, NULL,
                      metadata, &sysmd, buffer, buflen);
    if (dplret != DPL_SUCCESS)
    {
        ret = EXIT_FAILURE;
        goto end;
    }

    status_digest_add(ctx->status->digest, DIGEST_DONE_BYTES, filestate->fixed.size);

    ret = EXIT_SUCCESS;

end:
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
transfer_file(struct cloudmig_ctx* ctx,
              struct file_transfer_state* filestate)
{
    int                     ret;

    cloudmig_log(INFO_LVL,
    "[Migrating] : file '%s' is a regular file : starting transfer...\n",
    filestate->obj_path);


    if (ctx->tinfos[0].config_flags & AUTO_CREATE_DIRS)
    {
        if (create_parent_dirs(ctx, filestate) == EXIT_FAILURE)
        {
            ret = EXIT_FAILURE;
            goto err;
        }
    }

    ret = (filestate->fixed.size > ctx->options.block_size) ?
            transfer_chunked(ctx, filestate) : transfer_whole(ctx, filestate);

    cloudmig_log(INFO_LVL, "[Migrating] File '%s' transfer %s !\n",
                 filestate->obj_path, ret == EXIT_SUCCESS ? "succeeded" : "failed");

err:
    return ret;
}

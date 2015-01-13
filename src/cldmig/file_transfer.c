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

#include "cloudmig.h"
#include "options.h"


static int
create_parent_dirs(struct cloudmig_ctx *ctx,
                   struct file_transfer_state *filestate)
{
    char    *nextdelim = filestate->dst.name;
    dpl_status_t    dplret = DPL_SUCCESS;

    cloudmig_log(INFO_LVL,
                 "[Migrating]: Creating parent directories of file %s.\n",
                 filestate->src.name);

    while ((nextdelim = strchr(nextdelim, '/')) != NULL)
    {
        /*
         * TODO FIXME Do it with right attributes
         * FIXME : WORKAROUND : replace the current delimiter by a nul char
         * Since the dpl_mkdir function seems to fail when the last char is a delim
         */
        *nextdelim = '\0';
        cloudmig_log(INFO_LVL, "[Migrating]: Creating parent directory %s.\n",
                     filestate->dst.name);
        dplret = dpl_mkdir(ctx->dest_ctx, filestate->dst.name, NULL/*MD*/, NULL/*SYSMD*/);
        *nextdelim = '/';
        cloudmig_log(DEBUG_LVL,
                     "[Migrating]: Parent directory creation status : %s.\n",
                     dpl_status_str(dplret));
        if (dplret != DPL_SUCCESS && (dplret != DPL_ENOENT || dplret != DPL_EEXIST))
            goto err;
        // Now bypass current delimiter.
        nextdelim++;
    }
    cloudmig_log(INFO_LVL,
                 "[Migrating]: Parent directories created with success !\n");
    return EXIT_SUCCESS;
err:
    PRINTERR("[Migrating]: Could not create parent directories : %s\n",
             dpl_status_str(dplret));
    return EXIT_FAILURE;
}

/*
 * This callback receives the data read from a source,
 * and writes it into the destination file.
 */
static dpl_status_t
transfer_data_chunk(struct cloudmig_ctx *ctx,
                    struct file_transfer_state *filestate,
                    dpl_vfile_t *src, dpl_vfile_t *dst)
{
    dpl_status_t            ret = DPL_FAILURE;
    struct json_object      *src_status = NULL;
    struct json_object      *dst_status = NULL;
    char                    *buffer = NULL;
    unsigned int            buflen = 0;
    // For ETA data
    struct cldmig_transf    *e;
    struct timeval          tv;

    cloudmig_log(DEBUG_LVL,
                 "[Migrating]: %s : Transfering data chunk of %lu bytes.\n",
                 filestate->src.name, ctx->options.block_size);

    ret = dpl_fstream_get(src, ctx->options.block_size,
                          &buffer, &buflen, &src_status);
    if (ret != DPL_SUCCESS)
    {
        PRINTERR("Could not get next block from source file %s: %s",
                 filestate->src.name, dpl_status_str(ret));
        ret = DPL_FAILURE;
        goto end;
    }

    ret = dpl_fstream_put(dst, buffer, buflen, &dst_status);
    if (ret != DPL_SUCCESS)
    {
        PRINTERR("Could not put next block to destination file %s: %s",
                 filestate->dst.name, dpl_status_str(ret));
        ret = DPL_FAILURE;
        goto end;
    }

    /*
     * Here, create an element for the byte rate computing list
     * and insert it in the right info list.
     */
    // XXX we should use the thread_idx here instead of hard-coded index
    ctx->tinfos[0].fdone += len;
    gettimeofday(&tv, NULL);
    e = new_transf_info(&tv, len);
    if (e == NULL)
        PRINTERR("Could not update ETA block list, ETA might become erroneous");
    else
        insert_in_list(&ctx->tinfos[0].infolist, e);

    if (filestate->src.status)
        json_object_put(filestate->src.status);
    filestate->src.status = src_status;
    src_status = NULL;

    if (filestate->dst.status)
        json_object_put(filestate->dst.status);
    filestate->dst.status = dst_status;
    dst_status = NULL;

    filestate->fixed.offset += buflen;

    ret = DPL_SUCCESS;

end:
    if (buffer)
        free(buffer);
    if (src_status)
        json_object_put(src_status);
    if (dst_status)
        json_object_put(dst_status);

    return ret; /* dpl_write(data->hfile, buf, len); */
}

int
transfer_chunked(struct cloudmig_ctx *ctx,
                 struct file_transfer_state *filestate)
{
    int             ret = EXIT_FAILURE;
    dpl_status_t    dplret = DPL_FAILURE;
    dpl_vfile_t     *src = NULL;
    dpl_vfile_t     *dst = NULL;

    /*
     * Open the source file for reading
     */
    dplret = dpl_open(ctx->src_ctx, filestate->src.name,
                      DPL_VFILE_FLAG_RDONLY|DPL_VFILE_FLAG_STREAM,
                      NULL /* opts */, NULL /* cond */,
                      NULL/* MD */, NULL /* sysmd */,
                      NULL /* query params */,
                      filestate->src.status,
                      &src);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not open source file %s: %s\n",
                 __FUNCTION__, filestate->src.name, dpl_status_str(dplret));
        goto err;
    }

    /*
     * Open the destination file for writing
     */
    dplret = dpl_open(ctx->dest_ctx, filestate->dst.name,
                      DPL_VFILE_FLAG_CREAT|DPL_VFILE_FLAG_WRONLY|DPL_VFILE_FLAG_STREAM,
                      NULL /* opts */, NULL /* cond */,
                      NULL /*MD*/, NULL/*SYSMD*/,
                      NULL /* query params */,
                      filestate->dst.status,
                      &dst);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not open dest file %s: %s\n",
                 __FUNCTION__, filestate->dst.name, dpl_status_str(dplret));
        goto err;
    }

    /* Transfer the actual data */
    while (filestate->fixed.offset < filestate->fixed.size)
    {
        ret = transfer_data_chunk(ctx, filestate, src, dst);
        if (ret != EXIT_SUCCESS)
            goto err;

        /*
         * XXX TODO FIXME TODO XXX
         * Update bucket status
         * XXX TODO FIXME TODO XXX
        ret = transfer_state_save(ctx, filestate);
        if (ret != EXIT_SUCCESS)
            goto err;
         */
    }

    /*
     * Flush the destination stream to ensure everything is written and comitted
     */
    dplret = dpl_fstream_flush(dst);
    if (DPL_SUCCESS != dplret)
    {
        PRINTERR("%s: Could not flush destination file %s: %s",
                __FUNCTION__, filestate->dst.name, dpl_status_str(dplret));
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
                     __FUNCTION__, filestate->dst.name,
                     dpl_status_str(dplret));
        }
    }

    if (src)
    {
        dplret = dpl_close(src);
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("%s: Could not close source file %s: %s\n",
                     __FUNCTION__, filestate->src.name,
                     dpl_status_str(dplret));
        }
    }

    return ret;
}

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

    dplret = dpl_fget(ctx->src_ctx, filestate->src.name, NULL, NULL, NULL,
                      &buffer, &buflen, &metadata, &sysmd);
    if (dplret != DPL_SUCCESS)
    {
        ret = EXIT_FAILURE;
        goto end;
    }

    dplret = dpl_fput(ctx->dest_ctx, filestate->dst.name, NULL, NULL, NULL,
                      metadata, &sysmd, buffer, buflen);
    if (dplret != DPL_SUCCESS)
    {
        ret = EXIT_FAILURE;
        goto end;
    }

    /*
     * XXX TODO FIXME TODO XXX
     * Update bucket status
     * XXX TODO FIXME TODO XXX
     */

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
    int                     ret = EXIT_FAILURE;

    cloudmig_log(INFO_LVL,
    "[Migrating] : file '%s' is a regular file : starting transfer...\n",
    filestate->src.name);


    if (ctx->tinfos[0].config_flags & AUTO_CREATE_DIRS)
    {
        if (create_parent_dirs(ctx, filestate) == EXIT_FAILURE)
            goto err;
    }

    ret = (filestate->fixed.size > ctx->options.block_size) ?
          transfer_chunked(ctx, filestate)
        : transfer_whole(ctx, filestate);

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
                 filestate->src.name);

err:

    return ret;
}


int
create_directory(struct cloudmig_ctx* ctx,
                 struct file_transfer_state* filestate)
{ 
    int             ret = EXIT_FAILURE;
    dpl_status_t    dplret = DPL_SUCCESS;

    cloudmig_log(INFO_LVL,
"[Migrating] : file '%s' is a directory: creating dest dir...\n",
             filestate->src.name);

    /* 
     * FIXME : WORKAROUND : replace the last delimiter by a nul char
     * Since the dpl_mkdir function seems to fail when the last char is a delim
     */
    filestate->dst.name[strlen(filestate->dst.name) - 1] = 0;
    dplret = dpl_mkdir(ctx->dest_ctx, filestate->dst.name, NULL/*MD*/, NULL/*SYSMD*/);
    filestate->dst.name[strlen(filestate->dst.name)] = '/';
    // TODO FIXME With correct attributes
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not create destination dir '%s': %s\n",
                 __FUNCTION__, filestate->dst.name, dpl_status_str(dplret));
        goto end;
    }

    ret = EXIT_SUCCESS;

    /* No general status size to update, since directories are empty files */

    cloudmig_log(INFO_LVL,
                 "[Migrating] : directory '%s' successfully created !\n",
                 filestate->dst.name);

end:

    return ret;
}


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
int
transfer_file(struct cloudmig_ctx* ctx,
              struct file_transfer_state* filestate)
{
    int                     ret = EXIT_FAILURE;
    dpl_status_t            dplret;
    struct data_transfer    cb_data = { .hfile=NULL, .ctx=ctx };
    dpl_canned_acl_t        can_acl;

    cloudmig_log(INFO_LVL,
    "[Migrating] : file '%s' is a regular file : starting transfer...\n",
    filestate->name);


    /*
     * First, open the destination file for writing, after
     * retrieving the source file's canned acl.
     */
    can_acl = get_file_canned_acl(ctx->src_ctx, filestate->name);
    dplret = dpl_openwrite(ctx->dest_ctx, filestate->dst,
                           DPL_VFILE_FLAG_CREAT,
                           NULL, // metadata
                           can_acl,
                           filestate->fixed.size,
                           &cb_data.hfile);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not open dest file %s: %s\n",
                 __FUNCTION__, filestate->dst, dpl_status_str(dplret));
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
        PRINTERR("%s: Could not open source file %s: %s\n",
                 __FUNCTION__, filestate->name, dpl_status_str(dplret));
        goto err;
    }

    /* And finally, close the destination file written... */
    dplret = dpl_close(cb_data.hfile);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not close destination file %s: %s\n",
                 __FUNCTION__, filestate->dst,
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

    return ret;
}


int
create_directory(struct cloudmig_ctx* ctx,
                 struct file_transfer_state* filestate)
{ 
    int             ret = EXIT_FAILURE;
    dpl_status_t    dplret = DPL_SUCCESS;

    cloudmig_log(INFO_LVL,
                 "[Migrating] : file '%s' is a directory : creating dest dir...\n",
                 filestate->name);

    /* 
     * FIXME : WORKAROUND : replace the last slash by a nul char
     * Since the dpl_mkdir function seems to fail when the last char is a slash
     */
    filestate->dst[strlen(filestate->dst) - 1] = 0;
    dplret = dpl_mkdir(ctx->dest_ctx, filestate->dst);
    filestate->dst[strlen(filestate->dst)] = '/';
    // TODO FIXME With correct attributes
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not create destination dir '%s': %s\n",
                 __FUNCTION__, filestate->dst, dpl_status_str(dplret));
        goto end;
    }

    ret = EXIT_SUCCESS;

    /* No general status size to update, since directories are empty files */

    cloudmig_log(INFO_LVL,
                 "[Migrating] : directory '%s' successfully created !\n",
                 filestate->dst);

end:

    return ret;
}


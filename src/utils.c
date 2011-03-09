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

#include <unistd.h>

#include "cloudmig.h"

/*
 * Callback function for dpl_openread (needs a callback for each chunk
 * of data received)
 *
 * It appends each chunk of data to the buf of the transfer_state
 */
static dpl_status_t cloudmig_append_buffer_to_bucket_state(void* ctx,
                                                           char* buf,
                                                           unsigned int len)
{
    struct transfer_state* state = ctx;
    dpl_status_t    ret = DPL_FAILURE;

    int newlen = state->next_entry_off + len;
    state->buf = realloc(state->buf, newlen);
    if (state->buf == NULL)
    {
        ret = DPL_ENOMEM;
        goto err;
    }
    memmove(state->buf + state->next_entry_off, buf, len);
    state->next_entry_off += len;

    ret = DPL_SUCCESS;

err:
    
    return ret;
}


/*
 * Maps a bucket state's file into memory using dpl_openread.
 */
int cloudmig_map_bucket_state(struct cloudmig_ctx* ctx,
                              struct transfer_state* bucket_state)
{
    dpl_status_t        dplret;
    int                 ret = EXIT_FAILURE;
    dpl_dict_t          *metadata;
    char*               ctx_bucket = ctx->src_ctx->cur_bucket;

    /*
     * This function uses the bucket_state as private data for the callback.
     *
     * It uses the field next_entry_off to set the quantity of data received
     * so it needs to be reset to 0 afterwards (done in func end).
     */
    ctx->src_ctx->cur_bucket = ctx->status.bucket_name;
    bucket_state->next_entry_off = 0;
    dplret = dpl_openread(ctx->src_ctx, bucket_state->filename,
                          DPL_VFILE_FLAG_MD5,
                          NULL, // condition
                          &cloudmig_append_buffer_to_bucket_state, bucket_state,
                          &metadata);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not map bucket status file %s : %s\n",
                 __FUNCTION__, bucket_state->filename, dpl_status_str(dplret));
        goto err;
    }

    ret = EXIT_SUCCESS;

err:
    // Restore original bucket.
    ctx->src_ctx->cur_bucket = ctx_bucket;

    bucket_state->next_entry_off = 0;

    if (metadata != NULL)
        dpl_dict_free(metadata);

    return ret;
}


/*
 * This function allocates and fills a string with the status filename
 * matching the bucket given as parameter.
 */
char* cloudmig_get_status_filename_from_bucket(char* bucket)
{
    int     len = 0;
    char    *filename = NULL;

    len = strlen(bucket) + 10;
    filename = malloc(len * sizeof(*filename));
    if (filename == NULL)
    {
        PRINTERR("%s: Could not allocate memory"
                 " for status filename for bucket '%s'.\n",
                 __FUNCTION__, bucket);
        return NULL;
    }
    strcpy(filename, bucket);
    strcat(filename, ".cloudmig");
    return filename;
}

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

#ifndef __CLOUDMIG_STATUS_STORE_H__
#define __CLOUDMIG_STATUS_STORE_H__

struct cloudmig_ctx;

struct cloudmig_status* status_store_new();
void                    status_store_free(struct cloudmig_status *status);
void                    status_store_delete(struct cloudmig_ctx *ctx);

int status_store_load(struct cloudmig_ctx* ctx, char *src_host, char *dst_host);

void status_store_reset_iteration(struct cloudmig_ctx *ctx);

/**
 *
 * @return  1 - SUCCESS - Entry found
 *          0 - SUCCESS - No entry found (reached the end of the status'associated files)
 *         -1 - FAILURE - an error occurred, see log
 */
int     status_store_next_incomplete_entry(struct cloudmig_ctx *ctx,
                                           struct file_transfer_state *filestate);
int     status_store_next_entry(struct cloudmig_ctx *ctx, struct file_transfer_state *filestate);
void    status_store_release_entry(struct file_transfer_state *filestate);



/*
 * Functions to update an entry's status
 */
int     status_store_entry_update(struct cloudmig_ctx *ctx,
                                  struct file_transfer_state *filestate,
                                  uint64_t done_size);
int     status_store_entry_complete(struct cloudmig_ctx *ctx,
                                    struct file_transfer_state *filestate);

#endif /* ! __CLOUDMIG_STATUS_STORE_H__ */

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

#ifndef __CLOUDMIG_STATUS_BUCKET_H__
#define __CLOUDMIG_STATUS_BUCKET_H__

struct cloudmig_ctx;
struct cloudmig_status;
struct bucket_status;
struct file_transfer_state;

int                     status_bucket_namecmp(const char *ref, const char *optstring);

struct bucket_status*   status_bucket_new();
void                    status_bucket_free(struct bucket_status *bst);

int                     status_bucket_dup_paths(struct bucket_status *bst,
                                                char **statusp, char **srcp);

struct bucket_status*   status_bucket_load(dpl_ctx_t *status_ctx,
                                           char *storepath, char *name,
                                           uint64_t *countp, uint64_t *sizep);
struct bucket_status*   status_bucket_create(dpl_ctx_t *status_ctx, dpl_ctx_t *src_ctx,
                                             char *storepath, char *src, char *dst,
                                             uint64_t *countp, uint64_t *sizep);
void                    status_bucket_delete(dpl_ctx_t *status_ctx,
                                             struct bucket_status *bst);

void                    status_bucket_reset_iteration(struct bucket_status *bst);

/**
 *
 * @return  1 - SUCCESS - Entry found
 *          0 - SUCCESS - No entry found (reached the end of the status'associated files)
 *         -1 - FAILURE - an error occurred, see log
 */
int                     status_bucket_next_incomplete_entry(dpl_ctx_t *status_ctx,
                                                            struct bucket_status *bst,
                                                            struct file_transfer_state *filestate);
int                     status_bucket_next_entry(dpl_ctx_t *status_ctx,
                                                 struct bucket_status *bst,
                                                 struct file_transfer_state *filestate);
void                    status_bucket_release_entry(struct file_transfer_state *filestate);

/*
 * Function to upate one given entry within a status file.
 */
int     status_bucket_entry_update(dpl_ctx_t *ctx, struct file_transfer_state *filestate);
int     status_bucket_entry_complete(dpl_ctx_t *ctx, struct file_transfer_state *filestate);

#endif /* ! __CLOUDMIG_STATUS_BUCKET_H__ */

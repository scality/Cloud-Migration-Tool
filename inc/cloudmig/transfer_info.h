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

#ifndef __CLOUDMIG_TRANSFER_H__
#define __CLOUDMIG_TRANSFER_H__

/*
 * These structures and functions are used to manage and compute the real-time
 * ETA of the cloudmig tool.
 */

struct cldmig_transf
{
    struct timeval          t;      // timestamp for the moment of the transfer
    uint32_t                q;      // quantity transfered at time ts
    struct cldmig_transf    *next;
};

struct cldmig_transf    *new_transf_info(struct timeval *tv, uint32_t q);
void                    insert_in_list(struct cldmig_transf **list,
                                       struct cldmig_transf *item);
void                    remove_old_items(struct timeval *diff,
                                         struct cldmig_transf **list);
uint32_t                make_list_transfer_rate(struct cldmig_transf *list);
void                    clear_list(struct cldmig_transf **list);


#endif /* ! __CLOUDMIG_TRANSFER_H__ */

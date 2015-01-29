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

#ifndef __TRANSFER_STATE_H__
#define __TRANSFER_STATE_H__

#include <pthread.h>

/*
 * This macro rounds a number to superior 4
 * ex: 15 -> 16
 *     16 -> 20
 *     17 -> 20
 * This allows to get a len mod 4 with at least one '\0' caracter,
 */
#define ROUND_NAMLEN(namlen) ((namlen) + (4 - (namlen) % 4))



/***********************************************************************\
*                                                                       *
* These structs are used for the file that describe the migration status*
*                        ie : the file ".cloudmig"                      *
*                                                                       *
\***********************************************************************/

struct dpl_ctx;

struct status_digest
{
    struct {
        uint64_t        bytes;
        uint64_t        done_bytes;
        uint64_t        objects;
        uint64_t        done_objects;
    }               fixed;

    struct dpl_ctx  *status_ctx;

    char            *path;

    pthread_mutex_t lock;
    int             lock_inited;

    int             refresh_frequency;
    int             refresh_count;
};

/***********************************************************************\
*                                                                       *
* These structs are used for the files that describe a bucket's status. *
*                                                                       *
\***********************************************************************/
/*
 * File state entry structure
 */
struct file_state_entry
{
    uint64_t    size;       // size of the file
    uint64_t    offset;     // offset/quantity already copied
    /*
    int8_t      dpl_location;
    int8_t      dpl_acl;
    int16_t     padding;
    */
};

/*
 * Structure used to describe an entry in the bucket status file
 */
struct file_transfer_state
{
    struct bucket_status    *bst;

    struct file_state_entry fixed;
    struct json_object      *rstatus;   // Read status (from source)
    struct json_object      *wstatus;   // Write status (to dest)

    // Data allowing to retrieve easily where does this entry come from
    char                    *obj_path;
    char                    *status_path;
    int                     state_idx;
};

#define CLOUDMIG_FILESTATE_INITIALIZER  \
    {                                   \
        NULL,                           \
        { 0, 0 },                       \
        NULL,                           \
        NULL,                           \
        NULL,                           \
        NULL,                           \
        0                               \
    }




/***********************************************************************\
*                                                                       *
*           These structs are used for the status management            *
*                                                                       *
\***********************************************************************/
/*
 * Describes a bucket status file.
 */
struct bucket_status
{
    pthread_mutex_t             lock;
    int                         lock_inited;
    struct json_object          *json;          // File's Json representation
    char                        *path;          // path to the bucket status file
    unsigned int                refcount;       // Nb of refs currently held to it or its data
    unsigned int                next_entry;     // index to the next entry
};

/*
 * Structure containing the data about the whole transfer status
 */
struct cloudmig_status
{
    pthread_mutex_t         lock;
    int                     lock_inited;

    char                    *store_path;        // path of the status store (bucket or directory)
    int                     path_is_bucket;

    struct status_digest    *digest;            // general cloudmig status
    struct bucket_status    **buckets;          // ptr on the table of states
    int                     n_buckets;          // number of bucket states
    int                     cur_bucket;          // index of the current state
    int                     n_loaded;
};


#endif /* ! __TRANSFER_STATE_H__ */

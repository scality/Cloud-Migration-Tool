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

// Unused yet...
struct cldmig_state_header
{
    uint64_t    total_sz; // Can count up to ULLONG_MAX bytes.
    uint64_t    done_sz;
    uint64_t    nb_objects;
    uint64_t    done_objects;
};

struct cldmig_state_entry
{
    int32_t     file;
    int32_t     bucket;
};

struct cldmig_entry
{
    struct cldmig_state_entry   namlens;
    char                        *filename;
    char                        *bucket;
    struct cldmig_entry         *next;
};

struct cldmig_status
{
    struct cldmig_state_header  head;
    size_t                      size;
    char                        *buf;
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
    int32_t     namlen;     // calculated with ROUND_NAMLEN
    int32_t     size;       // size of the file
    int32_t     offset;     // offset/quantity already copied
    /*
    int8_t      dpl_location;
    int8_t      dpl_acl;
    int16_t     padding;
    */
};

/*
 * Structure used for an entry in the bucket status file
 */
struct file_transfer_state
{
    struct file_state_entry fixed;  // fixed
    char                    *name;  // name of the file

    // Data allowing to retrieve easily where does this entry come from
    int                     state_idx;
    unsigned int            offset;
};




/***********************************************************************\
*                                                                       *
*           These structs are used for the status management            *
*                                                                       *
\***********************************************************************/
/*
 * Describes a bucket status file.
 */
struct transfer_state
{
    char                        *filename;      // name of the status file
    char                        *dest_bucket;   // name of the destination bckt
    unsigned int                use_count;
    size_t                      size;           // size of the status file
    char                        *buf;           // file's content.
    unsigned int                next_entry_off; // index on the next entry
};

/*
 * Structure containing the data about the whole transfer status
 */
struct cloudmig_status
{
    struct cldmig_status    general;            // general cloudmig status
    char                    *bucket_name;       // name of the status bucket
    int                     nb_states;          // number of bucket states
    int                     cur_state;          // index of the current state
    struct transfer_state   *bucket_states;     // ptr on the table of states
};


#endif /* ! __TRANSFER_STATE_H__ */

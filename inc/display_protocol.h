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


#ifndef __CLOUDMIG_DISPLAY_PROTOCOL_H__
#define __CLOUDMIG_DISPLAY_PROTOCOL_H__


enum e_display_header
{
    GLOBAL_INFO     = 0,
    THREAD_INFO     = 1,
    MSG             = 2
};

enum msg_type
{
    DEFAULT     = 0,
    TEST        = 1
};

struct cldmig_global_info
{
    uint64_t    total_sz; // Can count up to ULLONG_MAX bytes.
    uint64_t    done_sz;
    uint64_t    nb_objects;
    uint64_t    done_objects;
};

struct cldmig_thread_info
{
    uint32_t    id;
    uint32_t    fsize;
    uint32_t    fdone;
    uint32_t    byterate;
    uint32_t    namlen;
};

#endif /* ! __CLOUDMIG_DISPLAY_PROTOCOL_H__ */

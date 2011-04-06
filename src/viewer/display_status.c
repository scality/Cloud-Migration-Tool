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

#include <ncurses.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "status.h"
#include "display_protocol.h"
#include "data.h"

void
display(struct cldmig_global_info *ginfo,
        struct thread_info* thr_data, uint32_t thr_nb,
        struct message* msgs)
{
    int line = 0;
    mvprintw(line++, 0, "Global infos : %llu/%llu bytes, %llu/%llu objects.",
             ginfo->done_sz, ginfo->total_sz,
             ginfo->done_objects, ginfo->nb_objects);
    for (uint32_t i =0; i < thr_nb; ++i)
    {
        mvprintw(line++, 0,
                 "Thread[%i][%s] : %u/%u bytes, speed: %u byte per second.",
                 i, thr_data[i].name, thr_data[i].sz_done,
                 thr_data[i].size, thr_data[i].byterate);
    }

    if (msgs)
    {
        line = LINES - 1;
        mvprintw(line--, 0, "[%s] : %s",
                 msgs->type ? "HAS_MSG_TYPE" : "HAS_NO_MSG_TYPE", msgs->msg);
    }
    refresh();
}

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

#include <assert.h>
#include <ncurses.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "status.h"
#include "display_protocol.h"
#include "data.h"
#include "viewer.h"

static float
get_size_order(uint64_t size, char **str)
{
    float   q = (float)size;

    *str = "o";
    if (q > 1024.)
    {
        q /= 1024.;
        *str = "Ko";
        if (q > 1024.)
        {
            q /= 1024.;
            *str = "Mo";
            if (q > 1024.)
            {
                q /= 1024.;
                *str = "Go";
                if (q > 1024.)
                {
                    q /= 1024.;
                    *str = "To";
                }
            }
        }
    }
    return q;
}

static char *
get_eta(uint64_t done, uint64_t total, uint32_t byterate)
{
    char        *str = NULL;
    if (byterate == 0)
    {
        if (asprintf(&str, "??? ETA") == -1)
            return NULL;
        return str;
    }

    uint64_t    seconds = (total - done) / byterate;
    uint64_t    minutes = seconds / 60;
    uint64_t    hours = minutes / 60;
    uint64_t    days = hours / 24;

    hours %= 24;
    minutes %= 60;
    seconds %= 60;
    if (days)
    {
        if (asprintf(&str, "%llud%lluh ETA", days, hours) == -1)
            return NULL;
    }
    else if (hours)
    {
        if (asprintf(&str, "%lluh%llum ETA", hours, minutes) == -1)
            return NULL;
    }
    else
    {
        if (asprintf(&str, "%llum%llus ETA", minutes, seconds) == -1)
            return NULL;
    }
    return (str);
}

static void
print_global_line(uint64_t bdone, uint64_t btotal,
                  uint64_t done_obj, uint64_t nb_obj,
                  uint64_t byterate)
{
    char    *stats = NULL;
    char    *eta_str = get_eta(bdone, btotal, byterate);
    char    *msg = NULL;
    // nb of highlighted chars of the progress bar.
    int     nbhighlight = bdone * COLS / btotal;
    // More practical values for printing...
    char    *sz_str[3] = { NULL, NULL, NULL};
    float   float_values[3] = {
        get_size_order(bdone,    &sz_str[0]),
        get_size_order(btotal,   &sz_str[1]),
        get_size_order(byterate, &sz_str[2])
    };

    asprintf(&stats,
             "%llu/%llu objects (%.2f%s/%.2f%s)  %.2f%s/s  %s",
             done_obj, nb_obj,
             float_values[0], sz_str[0],
             float_values[1], sz_str[1],
             float_values[2], sz_str[2],
             eta_str);
    free(eta_str);
    eta_str = NULL;
    // Printf the whole line into a printable string
    asprintf(&msg, "%s%*s", "GLOBAL STATS", COLS - 12, stats);
    free(stats);
    stats = NULL;

    attron(COLOR_PAIR(progressbar_idx));
    mvprintw(0, 0, "%.*s", nbhighlight, msg);
    attroff(COLOR_PAIR(progressbar_idx));
    mvprintw(0, nbhighlight, "%s", &msg[nbhighlight]);
    free(msg);
}


static void
print_line(int thread_id, char *fname,
           uint32_t bdone, uint32_t btotal, uint32_t byterate)
{
    char    *stats = NULL;
    char    *tmp_str = NULL;
    char    *msg = NULL;
    if (btotal == 0)
    {
        mvprintw(thread_id + 2, 0, "Thread[%i] : inactive...", thread_id);
        return ;
    }
    // nb of highlighted chars of the progress bar.
    int     nbhighlight = bdone * COLS / btotal;
    // More practical values for printing...
    char    *sz_str[3] = { NULL, NULL, NULL};
    float   float_values[3] = {
        get_size_order(bdone,    &sz_str[0]),
        get_size_order(btotal,   &sz_str[1]),
        get_size_order(byterate, &sz_str[2])
    };

    asprintf(&stats, "%.2f%s/%.2f%s  %.2f%s/s",
             float_values[0], sz_str[0],
             float_values[1], sz_str[1],
             float_values[2], sz_str[2]);

    asprintf(&tmp_str, "Thread[%i] : %s", thread_id, fname);
    // Printf the whole line into a printable string
    asprintf(&msg, "%s%*s", tmp_str, COLS - strlen(tmp_str), stats);
    free(stats);
    stats = NULL;
    free(tmp_str);
    tmp_str = NULL;

    attron(COLOR_PAIR(progressbar_idx));
    mvprintw(thread_id + 2, 0, "%.*s", nbhighlight, msg);
    attroff(COLOR_PAIR(progressbar_idx));
    mvprintw(thread_id + 2, nbhighlight, "%s", &msg[nbhighlight]);
    free(msg);
}


void
display(struct cldmig_global_info *ginfo,
        struct thread_info* thr_data, uint32_t thr_nb,
        struct message* msgs)
{
    uint64_t    total_byterate = 0;
    uint64_t    added_done = 0;
    for (uint32_t i=0; i < thr_nb; ++i)
    {
        total_byterate += thr_data[i].byterate;
        added_done += thr_data[i].sz_done;
    }
    print_global_line(ginfo->done_sz + added_done, ginfo->total_sz,
                      ginfo->done_objects, ginfo->nb_objects,
                      total_byterate);

    for (uint32_t i =0; i < thr_nb; ++i)
    {
        print_line(i, thr_data[i].name,
                   thr_data[i].sz_done, thr_data[i].size,
                   thr_data[i].byterate);
        /*mvprintw(line++, 0,
                 "Thread[%i][%s] : %u/%u bytes, speed: %u byte per second.",
                 i, thr_data[i].name, thr_data[i].sz_done,
                 thr_data[i].size, thr_data[i].byterate);*/
    }

    if (msgs)
    {
        /*line = LINES - 1;
        mvprintw(line--, 0, "[%s] : %s",
                 msgs->type ? "HAS_MSG_TYPE" : "HAS_NO_MSG_TYPE", msgs->msg);*/
    }
    refresh();
}

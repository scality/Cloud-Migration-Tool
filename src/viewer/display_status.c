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
        if (asprintf(&str, "%llud%lluh ETA",
                     (long long unsigned int)days,
                     (long long unsigned int)hours) == -1)
            return NULL;
    }
    else if (hours)
    {
        if (asprintf(&str, "%lluh%llum ETA",
                     (long long unsigned int)hours,\
                     (long long unsigned int)minutes) == -1)
            return NULL;
    }
    else
    {
        if (asprintf(&str, "%llum%llus ETA",
                     (long long unsigned int)minutes,
                     (long long unsigned int)seconds) == -1)
            return NULL;
    }
    return (str);
}

static int
print_global_line(uint64_t bdone, uint64_t btotal,
                  uint64_t done_obj, uint64_t nb_obj,
                  uint64_t byterate)
{
    int     ret = EXIT_FAILURE;
    char    *stats = NULL;
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
    char    *eta_str = get_eta(bdone, btotal, byterate);
    if (eta_str == NULL)
        return EXIT_FAILURE;

    ret = asprintf(&stats,
             " %llu/%llu objects (%.2f%s/%.2f%s)  %.2f%s/s  %s",
             (long long unsigned int)done_obj, (long long unsigned int)nb_obj,
             float_values[0], sz_str[0],
             float_values[1], sz_str[1],
             float_values[2], sz_str[2],
             eta_str);
    free(eta_str);
    eta_str = NULL;
    if (ret == -1)
        return EXIT_FAILURE;

    // Printf the whole line into a printable string
    ret = asprintf(&msg, "%s%*s", "GLOBAL STATS", COLS - 12, stats);
    free(stats);
    stats = NULL;
    if (ret == -1)
        return EXIT_FAILURE;

    attron(COLOR_PAIR(progressbar_idx));
    mvprintw(0, 0, "%.*s", nbhighlight, msg);
    attroff(COLOR_PAIR(progressbar_idx));
    mvprintw(0, nbhighlight, "%.*s", COLS-nbhighlight, &msg[nbhighlight]);
    free(msg);
    return EXIT_SUCCESS;
}


static int
print_line(int thread_id, char *fname,
           uint32_t bdone, uint32_t btotal, uint32_t byterate)
{
    int     ret = EXIT_SUCCESS;
    char    *stats = NULL;
    char    *tmp_str = NULL;
    char    *msg = NULL;
    int     statlen = 0;
    int     tmplen = 0;
    if (btotal == 0)
    {
        mvprintw(thread_id + 2, 0, "Thread[%i] : inactive...", thread_id);
        return EXIT_SUCCESS;
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

    ret = asprintf(&stats, " %.2f%s/%.2f%s  %.2f%s/s",
             float_values[0], sz_str[0],
             float_values[1], sz_str[1],
             float_values[2], sz_str[2]);
    if (ret == -1)
        return EXIT_FAILURE;

    ret = asprintf(&tmp_str, "Thread[%i] : %s", thread_id, fname);
    if (ret == -1)
    {
        free(stats);
        return EXIT_FAILURE;
    }
    // Printf the whole line into a printable string
    tmplen = strlen(tmp_str);
    statlen = strlen(stats);
    ret = asprintf(&msg, "%.*s%s%*s",
                   tmplen + statlen > COLS ?  COLS-statlen-3 : COLS-statlen,
                   tmp_str,
                   tmplen + statlen > COLS ? "..." : "",
                   tmplen + statlen > COLS ? 0 : COLS - tmplen,
                   stats);
    free(stats);
    free(tmp_str);
    if (ret == -1)
        return EXIT_FAILURE;

    attron(COLOR_PAIR(progressbar_idx));
    mvprintw(thread_id + 2, 0, "%.*s", nbhighlight, msg);
    attroff(COLOR_PAIR(progressbar_idx));
    mvprintw(thread_id + 2, nbhighlight, "%.*s",
             COLS-nbhighlight, &msg[nbhighlight]);
    free(msg);
    return EXIT_SUCCESS;
}


int
display(struct cldmig_global_info *ginfo,
        struct thread_info* thr_data, uint32_t thr_nb,
        struct message* msgs)
{
    int         ret;
    uint64_t    total_byterate = 0;
    uint64_t    added_done = 0;

    for (uint32_t i=0; i < thr_nb; ++i)
    {
        total_byterate += thr_data[i].byterate;
        added_done += thr_data[i].sz_done;
    }
    ret = print_global_line(ginfo->done_sz + added_done, ginfo->total_sz,
                            ginfo->done_objects, ginfo->nb_objects,
                            total_byterate);
    if (ret == EXIT_FAILURE)
        return EXIT_FAILURE;

    for (uint32_t i =0; i < thr_nb; ++i)
    {
        ret = print_line(i, thr_data[i].name,
                         thr_data[i].sz_done, thr_data[i].size,
                         thr_data[i].byterate);
        if (ret == EXIT_FAILURE)
            return EXIT_FAILURE;
    }

    if (msgs)
    {
        /*line = LINES - 1;
        mvprintw(line--, 0, "[%s] : %s",
                 msgs->type ? "HAS_MSG_TYPE" : "HAS_NO_MSG_TYPE", msgs->msg);*/
    }
    refresh();
    return EXIT_SUCCESS;
}

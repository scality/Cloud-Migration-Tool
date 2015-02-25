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

#include <sys/types.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "transfer_info.h"

/*
 * this is a function instead of a macro for debugging purposes
 */
static int
time_is_lesser(struct timeval *left, struct timeval *right)
{
    return (left->tv_sec < right->tv_sec
            || (left->tv_sec == right->tv_sec
                && left->tv_usec < right->tv_usec));
}

struct cldmig_transf*
new_transf_info(struct timeval *tv, uint32_t q)
{
    struct cldmig_transf *elem = calloc(1, sizeof(*elem));
    if (!elem)
        return NULL;

    elem->t.tv_sec = tv->tv_sec;
    elem->t.tv_usec = tv->tv_usec;
    elem->q = q;

    return elem;
}


/*
 * Let's do a sorted insert so that first is youngest and last oldest
 */
void
insert_in_list(struct cldmig_transf **list, struct cldmig_transf *item)
{
    struct cldmig_transf* cur = NULL;

    if (*list == NULL)
    {
        *list = item;
        return ;
    }

    if (time_is_lesser(&(*list)->t, &item->t))
    {
        item->next = *list;
        *list = item;
        return ;
    }

    cur = *list;
    while (cur->next != NULL
           && !time_is_lesser(&cur->next->t, &item->t))
        cur = cur->next;
    item->next = cur->next;
    cur->next = item;
}

void
remove_old_items(struct timeval *limit, struct cldmig_transf **list)
{
    struct cldmig_transf *cur;
    struct cldmig_transf *tmp;
    if (*list == NULL)
        return ;

    cur = *list;
    if (time_is_lesser(&cur->t, limit))
    {
        *list = NULL;
    }
    else
    {
        // Search the first one to be deleted since it's sorted
        while (cur->next != NULL
               && !time_is_lesser(&cur->next->t, limit))
            cur = cur->next;

        // Break the linked list's chain
        tmp = cur;
        cur = cur->next;
        if (tmp->next)
            tmp->next = NULL;
    }

    // Now, free the elements removed.
    while (cur)
    {
        tmp = cur;
        cur = tmp->next;
        free(tmp);
    }
}

uint32_t
make_list_transfer_rate(struct cldmig_transf *list)
{
    struct cldmig_transf    *cur = list;
    double rate = 0.0;
    double time = 0.0;

    if (list == NULL)
        return 0;

    // sum all the data transfered.
    while (cur->next != NULL)
    {
        rate += cur->q;
        cur = cur->next;
    }

    // get the time elapsed between first and last
    time  = (double)list->t.tv_sec + (double)(list->t.tv_usec) * .000001f;
    time -= (double)cur->t.tv_sec  + (double)(cur->t.tv_usec)  * .000001f;

    // Then compute, and we get the byterate per second :)
    rate /= time;
    return (uint32_t)rate;
}

void
clear_list(struct cldmig_transf **list)
{
    if (*list == NULL)
        return ;

    struct cldmig_transf *tmp;
    while (*list)
    {
        tmp = *list;
        *list = tmp->next;
        free(tmp);
    }
}

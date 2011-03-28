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
 * Let's do a sorted insert so that : timeA > timeB > timeC ...
 */
void
insert_in_list(struct cldmig_transf **list, struct cldmig_transf *item)
{
    struct cldmig_transf* cur;

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
    // Search the first one to be deleted since it's sorted...
    while (cur->next != NULL
           && !time_is_lesser(&cur->next->t, limit))
        cur = cur->next;

    // Then remove the rest.
    while (cur && time_is_lesser(&cur->t, limit))
    {
        tmp = cur;
        cur = tmp->next;
        free(tmp);
    }

    /*
     * Now if the first one was to be deleted, it should be the only one left :
     */
    if (time_is_lesser(&(*list)->t, limit))
    {
        free(*list);
        *list = NULL;
    }
}

uint32_t
make_list_transfer_rate(struct cldmig_transf *list)
{
    struct cldmig_transf    *cur = list;
    float rate = 0.0;
    float time;

    // First sum up all the data transfered.
    while (cur->next != NULL)
    {
        rate += cur->q;
        cur = cur->next;
    }

    // First get the time elapsed between first and last
    time = list->t.tv_sec;
    time += (float)(list->t.tv_usec) * .000001;
    time -= (float)(cur->t.tv_usec) * .000001;
    time -= cur->t.tv_sec;

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

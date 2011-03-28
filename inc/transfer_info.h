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

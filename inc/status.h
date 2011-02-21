#ifndef __TRANSFER_STATE_H__
#define __TRANSFER_STATE_H__


/*
 * This macro rounds a number to superior 4
 * ex: 16 -> 16
 *     17 -> 20
 */
#define ROUND_NAMLEN(namlen) ((namlen) + (4 - (namlen) % 4) % 4)

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
    struct file_state_entry fixed;
    char                    *name;
};


/*
 * Describes a bucket status file.
 */
struct transfer_state
{
    char                        *filename;
    size_t                      size; // size of the state file
    char                        *buf; // Needed in order to update the file.
    unsigned int                next_entry_off;
};

/*
 * Structure containing the data about the whole transfer status
 */
struct cloudmig_status
{
    char                    *bucket_name;
    int                     nb_states;
    int                     cur_state;
    struct transfer_state   *bucket_states;
};


#endif /* ! __TRANSFER_STATE_H__ */

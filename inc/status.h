#ifndef __TRANSFER_STATE_H__
#define __TRANSFER_STATE_H__

/*
 * File state entry structure
 */
struct file_state_entry
{
    int32_t      namlen;     // Aligned on 4 for easiness
    int32_t      size;       // size of the file
    int32_t      offset;     // offset/quantity already copied
};

/*
 * Structure used as a mask for reading an entry in the bucket status file
 */
struct file_transfer_state
{
    struct file_state_entry *fixed; // fixed data struct (use as reading mask)
    char*                   name;   // filename of the manipulated file
};


/*
 * Describes a bucket status file.
 */
struct transfer_state
{
    char*                       filename;
    char*                       buffer;     // buffer of the file
    size_t                      size;       // size of the state file
    void*                       next_entry; // to tell whether
};

/*
 * Structure containing the data about the whole transfer status
 */
struct cloudmig_status
{
    char*                   bucket;
    struct transfer_state   *bucket_states;
};


#endif /* ! __TRANSFER_STATE_H__ */

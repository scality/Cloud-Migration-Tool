#ifndef __CLOUDMIG_DISPLAY_PROTOCOL_H__
#define __CLOUDMIG_DISPLAY_PROTOCOL_H__


enum e_display_header
{
    GLOBAL_INFO     = 0,
    THREAD_INFO     = 1,
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

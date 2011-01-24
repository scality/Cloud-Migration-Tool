#ifndef __SD_CLOUMIG_OPT_H__
#define __SD_CLOUMIG_OPT_H__

enum cloudmig_flags
{
    SRC_PROFILE_NAME    = 1,
    DEST_PROFILE_NAME   = 1 << 1,
    IGNORE_STATUS       = 1 << 2,
    DEBUG               = 1 << 3,
    QUIET               = 1 << 4,
};

struct cloudmig_options
{
    int         is_src_name;
	const char* src_profile;
    int         is_dest_name;
	const char* dest_profile;
    int         flags;
};

extern struct cloudmig_options* gl_options;

#endif /* ! __SD_CLOUMIG_OPT_H__ */

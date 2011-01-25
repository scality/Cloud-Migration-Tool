#include <dropletp.h>
#include <libgen.h>

#include "options.h"
#include "cloudmig.h"

int load_profiles(struct cloudmig_ctx* ctx)
{
    char* cpy = 0;
    char* profile_dir = 0;
    char* profile_name = 0;

    /*
     * First, load the source profile...
     */
    cpy = strdup(gl_options->src_profile);
    if (gl_options->flags & SRC_PROFILE_NAME)
        profile_name = cpy;
    else
    {
        profile_name = basename(cpy);
        profile_dir = dirname(cpy);
    }
    if ((ctx->src = dpl_ctx_new(profile_dir, profile_name)) == NULL)
    {
        PRINTERR("Could not load source profile : %s/%s",
                 profile_dir, profile_name);
        return (EXIT_FAILURE);
    }
    free(cpy);


    cpy = strdup(gl_options->dest_profile);
    profile_dir = 0;
    if (gl_options->flags & DEST_PROFILE_NAME)
        profile_name = cpy;
    else
    {
        profile_name = basename(cpy);
        profile_dir = dirname(cpy);
    }
    if ((ctx->dest = dpl_ctx_new(profile_dir, profile_name)) == NULL)
    {
        dpl_ctx_free(ctx->src);
        PRINTERR("Could not load destination profile : %s/%s",
                 profile_dir, profile_name);
        return (EXIT_FAILURE);
    }
    free(cpy);

    return (EXIT_SUCCESS);
}

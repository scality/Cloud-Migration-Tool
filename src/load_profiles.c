#include <string.h>

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
    if ((ctx->src_ctx = dpl_ctx_new(profile_dir, profile_name)) == NULL)
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
    if ((ctx->dest_ctx = dpl_ctx_new(profile_dir, profile_name)) == NULL)
    {
        dpl_ctx_free(ctx->src_ctx);
        PRINTERR("Could not load destination profile : %s/%s",
                 profile_dir, profile_name);
        return (EXIT_FAILURE);
    }
    free(cpy);


    /*
     * If the debug option was given, let's activate every droplet traces.
     */
    if (gl_options->flags & DEBUG)
    {
        ctx->src_ctx->trace_level =   DPL_TRACE_CONN | DPL_TRACE_IO
                                    | DPL_TRACE_HTTP | DPL_TRACE_SSL
                                    | DPL_TRACE_REQ  | DPL_TRACE_CONV
                                    | DPL_TRACE_VDIR | DPL_TRACE_VFILE
                                    | DPL_TRACE_BUF  ;
        ctx->dest_ctx->trace_level =  DPL_TRACE_CONN | DPL_TRACE_IO
                                    | DPL_TRACE_HTTP | DPL_TRACE_SSL
                                    | DPL_TRACE_REQ  | DPL_TRACE_CONV
                                    | DPL_TRACE_VDIR | DPL_TRACE_VFILE
                                    | DPL_TRACE_BUF  ;
    }

    return (EXIT_SUCCESS);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "options.h"
#include "cloudmig.h"


extern enum cloudmig_loglevel gl_loglevel;

int cloudmig_options_check(void)
{
    if (!gl_options->src_profile)
    {
        PRINTERR("No source defined for the migration.\n", 0);
        return (EXIT_FAILURE);
    }
    if (!gl_options->dest_profile)
    {
        PRINTERR("No destination defined for the migration.\n", 0);
        return (EXIT_FAILURE);
    }
    if (gl_options->flags & DEBUG
        && gl_options->flags & QUIET)
    {
        PRINTERR("Bad options : q and v are mutually exclusive.", 0);
        return (EXIT_FAILURE);
    }
    if (gl_options->flags & DEBUG)
        gl_loglevel = DEBUG_LVL;
    else if (gl_options->flags & QUIET)
        gl_loglevel = WARN_LVL;
    return (EXIT_SUCCESS);
}


// global var used with getopt
extern char* optarg;
/*
 *
 * Here we could have used the getopt_long format, but because it is
 * a GNU extension, we chose to avoid depending on it.
 *
 */
int retrieve_opts(int argc, char* argv[])
{
    char cur_opt = 0;
    while ((cur_opt = getopt(argc, argv, "-s:d:iqv")) != -1)
    {
        switch (cur_opt)
        {
        case 1:
            /*
             * Then we are using non-options arguments.
             * That should be a droplet profile name in the default profile path.
             */
            if (!gl_options->src_profile)
            {
                gl_options->flags |= SRC_PROFILE_NAME;
                gl_options->src_profile = optarg;
            }
            else if (!gl_options->dest_profile)
            {
                gl_options->flags |= DEST_PROFILE_NAME;
                gl_options->dest_profile = optarg;
            }
            else
            {
                PRINTERR("Unexpected argument : %s\n", optarg);
                return (EXIT_FAILURE);
            }
            break ;
        case 's':
            if (gl_options->flags & SRC_PROFILE_NAME
                || gl_options->src_profile)
            {
                PRINTERR("Source profile already defined.\n", 0);
                return (EXIT_FAILURE);
            }
            gl_options->src_profile = optarg;
            break ;
        case 'd':
            if (gl_options->flags & DEST_PROFILE_NAME
                || gl_options->dest_profile)
            {
                PRINTERR("Destination profile already defined.\n", 0);
                return (EXIT_FAILURE);
            }
            gl_options->dest_profile = optarg;
            break ;
        case 'i':
            gl_options->flags |= IGNORE_STATUS;
            break ;
        case 'q':
            gl_options->flags |= QUIET;
            break ;
        case 'v':
            gl_options->flags |= DEBUG;
            break ;
        default:
            // An error has already been printed by getopt...
            return (EXIT_FAILURE);
        }
    }
    if (cloudmig_options_check())
        return (EXIT_FAILURE);
    return (EXIT_SUCCESS);
}

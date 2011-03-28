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
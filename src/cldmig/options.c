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

#include <sys/stat.h>
#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "options.h"
#include "cloudmig.h"

static int
_options_setup_default_status(struct cloudmig_options *options)
{
    int  ret;
    char *profilepath = NULL;
    char *posixpath = NULL;
    FILE *profile = NULL;

    ret = asprintf(&profilepath, "/tmp/cldmig_status.profile");
    if (ret <= 0)
    {
        PRINTERR("Could not allocate path for default status profile.\n");
        ret = EXIT_FAILURE;
        goto err;
    }

    ret = asprintf(&posixpath, "/home/%s/.cloudmig", getenv("USER"));
    if (ret <= 0)
    {
        PRINTERR("Could not allocate path for default status profile.\n");
        ret = EXIT_FAILURE;
        goto err;
    }

    if (mkdir(posixpath, 0700) < 0)
    {
        PRINTERR("Cannot create local directory '%s'"
                 " for status storage.", posixpath);
        ret = EXIT_FAILURE;
        goto err;
    }

    profile = fopen(profilepath, "w+");
    if (profile == NULL)
    {
        PRINTERR("Cannot open status profile temp file.");
        ret = EXIT_FAILURE;
        goto err;
    }

    if (fprintf(profile, "backend=posix\nbase_path=%s\n", getenv("USER")) <= 0
        || fflush(profile) <= 0)
    {
        PRINTERR("Could not generated default profile for status storage.\n");
        ret = EXIT_FAILURE;
        goto err;
    }

    options->flags &= ~STATUS_PROFILE_NAME;
    options->status_profile = profilepath;
    profilepath = NULL;

    ret = EXIT_SUCCESS;

err:
    if (posixpath)
        free(posixpath);
    if (profilepath)
        free(profilepath);
    if (profile)
        fclose(profile);

    return ret;
}

int
cloudmig_options_check(struct cloudmig_options *options)
{
    if (!options->src_profile)
    {
        PRINTERR("No source defined for the migration.\n");
        return EXIT_FAILURE;
    }
    if (!options->dest_profile)
    {
        PRINTERR("No destination defined for the migration.\n");
        return EXIT_FAILURE;
    }
    if (!options->status_profile
        && _options_setup_default_status(options) != EXIT_SUCCESS)
    {
        PRINTERR("No status storage could be setup for the migration.\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int
opt_src_profile(struct cloudmig_options *options)
{
    if (options->flags & SRC_PROFILE_NAME || options->src_profile)
    {
        PRINTERR("Source profile already defined.\n", 0);
        return (EXIT_FAILURE);
    }
    options->src_profile = optarg;
    return (EXIT_SUCCESS);
}

static int
opt_dst_profile(struct cloudmig_options *options)
{
    if (options->flags & DEST_PROFILE_NAME
        || options->dest_profile)
    {
        PRINTERR("Destination profile already defined.\n", 0);
        return (EXIT_FAILURE);
    }
    options->dest_profile = optarg;
    return (EXIT_SUCCESS);
}

static int
opt_status_profile(struct cloudmig_options *options)
{
    if (options->flags & STATUS_PROFILE_NAME
        || options->status_profile)
    {
        PRINTERR("Status profile already defined.\n", 0);
        return (EXIT_FAILURE);
    }
    options->status_profile = optarg;
    return (EXIT_SUCCESS);
}

int
opt_trace(struct cloudmig_options *options, const char *arg)
{
    while (*arg)
    {
        switch (*arg)
        {
        case 'n': // network = connexion
            options->trace_flags |= DPL_TRACE_CONN;
            break ;
        case 'i': // io
            options->trace_flags |= DPL_TRACE_IO;
            break ;
        case 'h': // http
            options->trace_flags |= DPL_TRACE_HTTP;
            break ;
        case 's': // ssl
            options->trace_flags |= DPL_TRACE_SSL;
            break ;
        case 'r': // req = requests
            options->trace_flags |= DPL_TRACE_REQ;
            break ;
        case 'c': // conv = droplet conv api
            options->trace_flags |= DPL_TRACE_REST;
            break ;
        case 'd': // dir = droplet vdir api
            options->trace_flags |= DPL_TRACE_VFS;
            break ;
        case 'f': // file = droplet vfile api
            options->trace_flags |= DPL_TRACE_ID;
            break ;
        case 'b': // backend
            options->trace_flags |= DPL_TRACE_BACKEND;
            break ;
        default:
            PRINTERR(
                "Character %c is an invalid argument to droplet-trace option.\n"
                "See manpage for more informations.\n", *arg);
            return EXIT_FAILURE;
        }
        arg++;
    }
    return EXIT_SUCCESS;
}

int
opt_buckets(struct cloudmig_options *options, const char *arg)
{
    char    *src;
    char    *dst;
    int     size = 0;
    char    *next_coma = optarg;

    if (options->src_buckets && options->dst_buckets)
    {
        PRINTERR("Multiple Buckets association settings !\n", 0);
        return EXIT_FAILURE;
    }
    while (arg && *arg)
    {
        // copy until the next ':' character.
        src = strndup(arg, strcspn(arg, ":"));
        dst = index(arg, ':');
        if (dst)
        {
            ++dst; // Jump over the ':'
            // copy until the next semicolon (or end of string)
            dst = strndup(dst, strcspn(dst, ","));
        }
        next_coma = index(arg, ',');
        if (dst == NULL ||
            *dst == 0 ||
            (next_coma && dst > next_coma))
        {
            PRINTERR("The list of source/destination buckets is invalid.\n", 0);
            return (EXIT_FAILURE);
        }
        // goto the first char of the dst bucket name
        size += 1;
        // Realloc src tab and set additional src.
        options->src_buckets = realloc(options->src_buckets,
                                         sizeof(*options->src_buckets)
                                          * (size + 1));
        options->src_buckets[size - 1] = src;
        options->src_buckets[size] = NULL;
        // Realloc dst tab and set additional dst.
        options->dst_buckets = realloc(options->dst_buckets,
                                         sizeof(*options->dst_buckets)
                                          * (size + 1));
        options->dst_buckets[size - 1] = dst;
        options->dst_buckets[size] = NULL;

        arg = next_coma;
        if (arg)
            ++arg; // jump over the semicolon.
    }
    return (EXIT_SUCCESS);
}

int
opt_verbose(const char *arg)
{
    if (arg == NULL)
    {
        PRINTERR("Invalid verbose level: %s", arg);
        return EXIT_FAILURE;
    }

    else if (strcmp(arg, "debug") == 0)
        gl_loglevel = DEBUG_LVL;
    else if (strcmp(arg, "info") == 0)
        gl_loglevel = INFO_LVL;
    else if (strcmp(arg, "warn") == 0)
        gl_loglevel = WARN_LVL;
    else if (strcmp(arg, "status") == 0)
        gl_loglevel = STATUS_LVL;
    else if (strcmp(arg, "error") == 0)
        gl_loglevel = ERR_LVL;
    else
    {
        PRINTERR("Invalid verbose level: %s", arg);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

// global var used with getopt
extern char* optarg;
/*
 *
 * Here we could have used the getopt_long format, but because it is
 * a GNU extension, we chose to avoid depending on it.
 *
 */
int retrieve_opts(struct cloudmig_options *options, int argc, char* argv[])
{
    char                    cur_opt = 0;
    int                     option_index = 0;
    static struct option    long_options[] = {
//      {name, has_arg, flagptr, returned_val}
        /* Behavior-related options         */
        {"delete-source",       no_argument,        0,  0 },
        {"background",          no_argument,        0,  0 },
        {"create-directories",  no_argument,        0,  0 },
        {"block-size",          required_argument,  0, 'B'},
        /* Configuration-related options    */
        {"src-profile",         required_argument,  0, 's'},
        {"dst-profile",         required_argument,  0, 'd'},
        {"status-profile",      required_argument,  0, 'S'},
        {"buckets",             required_argument,  0, 'b'},
        {"config",              required_argument,  0, 'c'},
        /* Status-related options           */
//      {"ignore-status",       no_argument,        0, 'i'},
        {"force-resume",        no_argument,        0, 'r'},
        /* Verbose/Log-related options      */
        {"verbose",             required_argument,  0, 'v'},
        {"droplet-trace",       required_argument,  0, 't'},
        {"output",              required_argument,  0, 'o'},
        /* Last element                     */
        {0,                     0,                  0,  0 }
    };

    while ((cur_opt = getopt_long(argc, argv,
                                  "-B:s:d:S:b:c:r:v:t:o:",
                                  long_options, &option_index)) != -1)
    {
        switch (cur_opt)
        {
        case 0:
            // Manage all options without short equivalents :
            switch (option_index)
            {
            case 0: // delete-source
                options->flags |= DELETE_SOURCE_DATA;
                break ;
            case 1: // background mode
                // In background mode, the tool should be fully silent
                gl_isbackground = true;
                break ;
            case 2: // create-directories
                options->flags |= AUTO_CREATE_DIRS;
                break ;
            }
            break ;
        case 1:
            /*
             * Then we are using non-options arguments.
             * That should be a droplet profile name in the default profile path
             */
            if (!options->src_profile)
            {
                options->flags |= SRC_PROFILE_NAME;
                options->src_profile = optarg;
            }
            else if (!options->dest_profile)
            {
                options->flags |= DEST_PROFILE_NAME;
                options->dest_profile = optarg;
            }
            else if (!options->status_profile)
            {
                options->flags |= STATUS_PROFILE_NAME;
                options->status_profile = optarg;
            }
            else
            {
                PRINTERR("Unexpected argument : %s\n", optarg);
                return EXIT_FAILURE;
            }
            break ;
        case 's':
            if (opt_src_profile(options))
                return EXIT_FAILURE;
            break ;
        case 'd':
            if (opt_dst_profile(options))
                return EXIT_FAILURE;
            break ;
        case 'S':
            if (opt_status_profile(options))
                return EXIT_FAILURE;
            break ;
        case 'b':
            if (opt_buckets(options, optarg))
                return EXIT_FAILURE;
            break ;
        case 'B':
            options->block_size = strtoul(optarg, NULL, 10);
            if (options->block_size == ULONG_MAX && errno == ERANGE)
            {
                PRINTERR("Invalid value for block size argument");
                return EXIT_FAILURE;
            }
            break ;
        case 'c':
            if (options->config)
                return EXIT_FAILURE;
            options->config = optarg;
            break ;
        case 'r':
            options->flags |= RESUME_MIGRATION;
            break ;
        case 't':
            if (opt_trace(options, optarg))
                return EXIT_FAILURE;
            break;
/*      case 'i':
            options->flags |= IGNORE_STATUS;
            break ; */
        case 'v':
            if (opt_verbose(optarg) != EXIT_SUCCESS)
                return EXIT_FAILURE;
            break ;
        case 'o':
            options->logfile = optarg;
            break ;
        default:
            // An error has already been printed by getopt...
            return EXIT_FAILURE;
        }
    }
    if (options->config == NULL && cloudmig_options_check(options))
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

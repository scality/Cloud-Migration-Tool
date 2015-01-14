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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <json.h>

#include "cloudmig.h"
#include "options.h"

static int
create_tmp_profiles(char *srcpath, size_t srcsize,
                    char *dstpath, size_t dstsize,
                    char *stpath, size_t stsize,
                    FILE **src_profile, FILE **dst_profile, FILE **st_profile)
{
    int ret;
    FILE *src = NULL;
    FILE *dst = NULL;
    FILE *status = NULL;

    snprintf(srcpath, srcsize, "/tmp/cldmig_src.profile");
    snprintf(dstpath, dstsize, "/tmp/cldmig_dst.profile");
    snprintf(stpath,  stsize,  "/tmp/cldmig_status.profile");

    // Create the tmp files
    src = fopen(srcpath, "w+");
    if (src == NULL)
    {
        PRINTERR("[Loading Config]: Could not create temporary droplet"
                 " source profile: %s\n", strerror(errno));
        ret = EXIT_FAILURE;
        goto err;
    }

    dst = fopen(dstpath, "w+");
    if (dst == NULL)
    {
        PRINTERR("[Loading Config]: Could not create temporary droplet"
                 " destination profile: %s\n", strerror(errno));
        ret = EXIT_FAILURE;
        goto err;
    }

    status = fopen(stpath, "w+");
    if (status == NULL)
    {
        PRINTERR("[Loading Config]: Could not create temporary droplet"
                 " status profile: %s\n", strerror(errno));
        ret = EXIT_FAILURE;
        goto err;
    }

    *src_profile = src;
    src = NULL;

    *dst_profile = dst;
    dst = NULL;

    *st_profile = status;
    status = NULL;

    ret = EXIT_SUCCESS;

err:
    if (src)
    {
        fclose(src);
        unlink(srcpath);
    }
    if (dst)
    {
        fclose(dst);
        unlink(dstpath);
    }
    if (status)
    {
        fclose(status);
        unlink(stpath);
    }

    return ret;
}

static char*
map_config_file(char *file, size_t *len, int *fdp)
{
    char        *ret = NULL;
    char        *map = NULL;
    struct stat st;

    if ((*fdp = open(file, O_RDONLY)) == -1)
    {
        PRINTERR("[Loading Config]: Could not open config file : %s.\n",
                 strerror(errno));
        goto err;
    }
    if (fstat(*fdp, &st) == -1)
    {
        PRINTERR("[Loading Config]: Could not stat config file : %s.\n",
                 strerror(errno));
        goto err;
    }
    *len = st.st_size;
    map = mmap(map, *len, PROT_READ, MAP_FILE|MAP_PRIVATE, *fdp, 0);
    if (map == NULL)
    {
        PRINTERR("[Loading Config]: Could not map config file : %s.\n",
                 strerror(errno));
        goto err;
    }

    ret = map;
    fdp = NULL;

err:
    if (fdp && *fdp != -1)
        close(*fdp);

    return ret;
}

static void
unmap_config_file(int *fdp, void *addr, size_t size)
{
    if (munmap(addr, size) == -1)
        cloudmig_log(WARN_LVL,
                     "[Loaded Config]: Could not munmap config : %s\n",
                     strerror(errno));
    close(*fdp);
    *fdp = -1;
}

static int
config_update_json_buckets(struct cloudmig_options *options,
                           struct json_object *buckets)
{
    int ret = EXIT_SUCCESS;
    int i = 0;
    int n_buckets = 0;
    char **src_buckets = NULL;
    char **dst_buckets = NULL;

    n_buckets = json_object_object_length(buckets);

    src_buckets = calloc(n_buckets+1, sizeof(*options->src_buckets));
    dst_buckets = calloc(n_buckets+1, sizeof(*options->dst_buckets));
    if (src_buckets == NULL || dst_buckets == NULL)
    {
        PRINTERR("Could not allocate buckets for configuration.");
        ret = EXIT_FAILURE;
        goto err;
    }

    json_object_object_foreach(buckets, key, val)
    {
        if (json_object_is_type(val, json_type_string) == FALSE)
        {
            PRINTERR("Bucket \"%s\" 's target is not a json string", key);
            ret = EXIT_FAILURE;
            goto err;
        }
        src_buckets[i] = strdup(key);
        dst_buckets[i] = strdup(json_object_get_string(val));
        if (src_buckets[i] == NULL || dst_buckets[i] == NULL)
        {
            PRINTERR("Could not allocate buckets for configuration.");
            ret = EXIT_FAILURE;
            goto err;
        }
        ++i;
    }

    options->src_buckets = src_buckets;
    options->dst_buckets = dst_buckets;
    src_buckets = NULL;
    dst_buckets = NULL;

    ret = EXIT_SUCCESS;

err:
    if (src_buckets)
    {
        for (int i=0; i < n_buckets; i++)
            free(src_buckets[i]);
        free(src_buckets);
    }
    if (dst_buckets)
    {
        for (int i=0; i < n_buckets; i++)
            free(dst_buckets[i]);
        free(dst_buckets);
    }

    return ret;
}

/*
 * For each option, the configuration file should overwrite the command-line
 * arguments settings. If the option is not "true" for the flags, then unset it
 */
static int
config_update_options(struct cldmig_config *conf,
                      struct cloudmig_options *options,
                      const char *section, const char *key, struct json_object *val,
                      FILE *src_profile, FILE *dst_profile,
                      FILE *status_profile)
{
    int         add_to_profile = 0;
    int         *countp = NULL;
    FILE        *profile = NULL;

    if (strcasecmp(section, "source") == 0)
    {
        profile = src_profile;
        countp = &conf->src_entry_count;
        add_to_profile = 1;
    }
    else if (strcasecmp(section, "destination") == 0)
    {
        profile = dst_profile;
        countp = &conf->dst_entry_count;
        add_to_profile = 1;
    }
    else if (strcasecmp(section, "status") == 0)
    {
        profile = status_profile;
        countp = &conf->status_entry_count;
        add_to_profile = 1;
    }
    else if (strcasecmp(section, "cloudmig") == 0)
    {
        if (strcasecmp(key, "buckets") == 0)
        {
            if (!json_object_is_type(val, json_type_object))
            {
                PRINTERR("Unexpected type %i for option 'cloudmig/buckets'",
                         json_object_get_type(val));
                return EXIT_FAILURE;
            }
            if (options->src_buckets || options->dst_buckets)
            {
                PRINTERR("Source and target buckets cannot be configured multiple times.");
                return EXIT_FAILURE;
            }
            if (config_update_json_buckets(options, val) != EXIT_SUCCESS)
                return EXIT_FAILURE;
        }
        else if (strcasecmp(key, "force-resume") == 0)
        {
            if (!json_object_is_type(val, json_type_boolean))
            {
                PRINTERR("Unexpected type %i for option 'cloudmig/force-resume'",
                         json_object_get_type(val));
                return EXIT_FAILURE;
            }
            options->flags &= ~RESUME_MIGRATION;
            if (json_object_get_boolean(val) == TRUE)
                options->flags |= RESUME_MIGRATION;
        }
        else if (strcasecmp(key, "delete-source") == 0)
        {
            if (!json_object_is_type(val, json_type_boolean))
            {
                PRINTERR("Unexpected type %i for option 'cloudmig/delete-source'",
                         json_object_get_type(val));
                return EXIT_FAILURE;
            }
            options->flags &= ~DELETE_SOURCE_DATA;
            if (json_object_get_boolean(val) == TRUE)
                options->flags |= DELETE_SOURCE_DATA;
        }
        else if (strcasecmp(key, "background") == 0)
        {
            if (!json_object_is_type(val, json_type_boolean))
            {
                PRINTERR("Unexpected type %i for option 'cloudmig/background'",
                         json_object_get_type(val));
                return EXIT_FAILURE;
            }
            gl_isbackground = false;
            if (json_object_get_boolean(val) == TRUE)
                gl_isbackground |= true;
        }
        else if (strcasecmp(key, "verbose") == 0)
        {
            if (!json_object_is_type(val, json_type_string))
            {
                PRINTERR("Unexpected type %i for option 'cloudmig/verbose'",
                         json_object_get_type(val));
                return EXIT_FAILURE;
            }
            if (opt_verbose(json_object_get_string(val)) != EXIT_SUCCESS)
                return EXIT_FAILURE;
        }
        else if (strcasecmp(key, "droplet-trace") == 0)
        {
            if (!json_object_is_type(val, json_type_string))
            {
                PRINTERR("Unexpected type %i for option 'cloudmig/droplet-trace'",
                         json_object_get_type(val));
                return EXIT_FAILURE;
            }
            if (opt_trace(options, json_object_get_string(val)) != EXIT_SUCCESS)
                return EXIT_FAILURE;
        }
        else if (strcasecmp(key, "output") == 0)
        {
            if (!json_object_is_type(val, json_type_string))
            {
                PRINTERR("Unexpected type %i for option 'cloudmig/output'",
                         json_object_get_type(val));
                return EXIT_FAILURE;
            }
            const char *v = json_object_get_string(val);
            if (v[0] != '\0')
            {
                char *file = strdup(v);
                if (file == NULL)
                {
                    PRINTERR("[Loading Config]: Could not set logfile %s\n",
                             strerror(errno));
                    return EXIT_FAILURE;
                }
                options->logfile = file;
            }
        }
        else if (strcasecmp(key, "create-directories") == 0)
        {
            if (!json_object_is_type(val, json_type_boolean))
            {
                PRINTERR("Unexpected type %i for option 'cloudmig/create-directories'",
                         json_object_get_type(val));
                return EXIT_FAILURE;
            }
            options->flags &= ~AUTO_CREATE_DIRS;
            if (json_object_get_boolean(val) == TRUE)
                options->flags |= AUTO_CREATE_DIRS;
        }
    }
    else
        PRINTERR("[Loading Config]: Invalid section name '%s'.\n", section);

    if (add_to_profile)
    {
        switch (json_object_get_type(val))
        {
        case json_type_boolean:
            if (fprintf(profile, "%s=%i\n", key, !!json_object_get_boolean(val)) <= 0)
            {
                PRINTERR("Could not write entry from config into generated profile.");
                return EXIT_FAILURE;
            }
            break ;
        case json_type_double:
            if (fprintf(profile, "%s=%f\n", key, json_object_get_double(val)) <= 0)
            {
                PRINTERR("Could not write entry from config into generated profile.");
                return EXIT_FAILURE;
            }
            break ;
        case json_type_int:
            if (fprintf(profile, "%s=%i\n", key, json_object_get_int(val)) <= 0)
            {
                PRINTERR("Could not write entry from config into generated profile.");
                return EXIT_FAILURE;
            }
            break ;
        case json_type_string:
            if (fprintf(profile, "%s=%s\n", key, json_object_get_string(val)) <= 0)
            {
                PRINTERR("Could not write entry from config into generated profile.");
                return EXIT_FAILURE;
            }
            break ;
        case json_type_object:
        case json_type_array:
        default:
            PRINTERR("[Loading Config]: Unexpected json element type"
                     " for section's %s value: %i.", section,
                     json_object_get_type(val));
            return EXIT_FAILURE;
        }
        if (fflush(profile) <= 0)
        {
            PRINTERR("Could not fflush generated profile");
            return EXIT_FAILURE;
        }
        *countp += 1;
    }

    return EXIT_SUCCESS;
}

static int
parse_config(struct cldmig_config *conf,
             struct cloudmig_options *options,
             struct json_object *json_config,
             FILE *src_profile, FILE *dst_profile,
             FILE *status_profile)
{
    int     ret = EXIT_SUCCESS;

    json_object_object_foreach(json_config, section_name, section)
    {
        json_object_object_foreach(section, key, val)
        {
            if (config_update_options(conf, options,
                                      section_name, key, val,
                                      src_profile,
                                      dst_profile,
                                      status_profile) == EXIT_FAILURE)
                ret = EXIT_FAILURE;
        }
    }

    return ret;
}

int
load_config(struct cldmig_config *conf, struct cloudmig_options *options)
{
    int                 ret = EXIT_FAILURE;
    int                 config_fd = -1;
    char                *fbuf = NULL;
    size_t              fsize = 0;
    struct json_tokener *json_parser = NULL;
    struct json_object  *json_config = NULL;
    FILE                *src_profile = NULL;
    FILE                *dst_profile = NULL;
    FILE                *status_profile = NULL;
    int                 unlink_profiles = 1;

    cloudmig_log(DEBUG_LVL, "[Loading Config]: Starting configuration file parsing.\n");

    if (create_tmp_profiles(conf->src_profile, sizeof(conf->src_profile),
                            conf->dst_profile, sizeof(conf->dst_profile),
                            conf->status_profile, sizeof(conf->status_profile),
                            &src_profile, &dst_profile, &status_profile) == EXIT_FAILURE)
    {
        PRINTERR("[Loading Config]: Could not create temporary profiles.\n");
        ret = EXIT_FAILURE;
        goto err;
    }

    fbuf = map_config_file(options->config, &fsize, &config_fd);
    if (fbuf == NULL)
    {
        ret = EXIT_FAILURE;
        goto err;
    }

    json_parser = json_tokener_new();
    if (json_parser == NULL)
    {
        PRINTERR("[Loading Config]: Could not allocate json parser.\n");
        ret = EXIT_FAILURE;
        goto err;
    }
    json_config = json_tokener_parse_ex(json_parser, fbuf, fsize);
    if (json_config == NULL)
    {
        PRINTERR("[Loading Config]: Could not parse json.\n");
        ret = EXIT_FAILURE;
        goto err;
    }

    if (parse_config(conf, options, json_config,
                     src_profile, dst_profile, status_profile) == EXIT_FAILURE)
    {
        ret = EXIT_FAILURE;
        goto err;
    }

    /*
     * Configuration successfully loaded.
     *
     * - Ensure that configuration is overriden by CLI options.
     * - Ensure status to be the right default (local FS = posix backend)
     */
    if (options->src_profile == NULL && conf->src_entry_count > 0)
    {
        options->flags &= ~SRC_PROFILE_NAME;
        options->src_profile = conf->src_profile;
    }
    if (options->dest_profile == NULL && conf->dst_entry_count > 0)
    {
        options->flags &= ~DEST_PROFILE_NAME;
        options->dest_profile = conf->dst_profile;
    }
    if (options->status_profile == NULL && conf->status_entry_count > 0)
    {
        options->flags &= ~STATUS_PROFILE_NAME;
        options->status_profile = conf->status_profile;
    }

    if (cloudmig_options_check(options))
    {
        ret = EXIT_FAILURE;
        goto err;
    }

    unlink_profiles = 0;

    ret = EXIT_SUCCESS;

err:
    if (fbuf)
        unmap_config_file(&config_fd, fbuf, fsize);
    if (src_profile != NULL)
        fclose(src_profile);
    if (dst_profile != NULL)
        fclose(dst_profile);

    if (unlink_profiles)
    {
        unlink(conf->src_profile);
        unlink(conf->dst_profile);
        unlink(conf->status_profile);
    }

    if (json_parser)
        json_tokener_free(json_parser);
    if (json_config)
        json_object_put(json_config);

    return ret;
}

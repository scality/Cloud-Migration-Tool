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

#include "cloudmig.h"
#include "options.h"
extern int gl_loglevel;

static int      config_fd = -1;
static FILE     *src_profile = NULL;
static FILE     *dst_profile = NULL;

static int
create_tmpfiles(char *file1, char *file2, size_t size1, size_t size2)
{
    int     fd1 = -1;
    int     fd2 = -1;

    snprintf(file1, size1, "/tmp/cldmig_src.profile");
    snprintf(file2, size2, "/tmp/cldmig_dst.profile");
    // Create the tmp files
    src_profile = fopen(file1, "w+");
    if (src_profile == NULL)
    {
        close(fd1);
        close(fd2);
        return EXIT_FAILURE;
    }
    dst_profile = fopen(file2, "w+");
    if (dst_profile == NULL)
    {
        fclose(src_profile);
        close(fd2);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static char*
map_config_file(char *file, size_t *len)
{
    char        *map = NULL;
    struct stat st;

    if ((config_fd = open(file, O_RDONLY)) == -1)
        goto err;
    if (fstat(config_fd, &st) == -1)
        goto err;
    *len = st.st_size;
    map = mmap(map, *len,
               PROT_READ, MAP_FILE|MAP_PRIVATE, config_fd, 0);
    if (map == NULL)
        goto err;

    return map;

err:
    PRINTERR("[Loading Config]: Could not map config file : %s.\n",
             strerror(errno));
    if (config_fd != -1)
        close(config_fd);
    return NULL;
}

static void
unmap_config_file(void *addr, size_t size)
{
    if (munmap(addr, size) == -1)
        cloudmig_log(WARN_LVL,
                     "[Loaded Config]: Could not munmap config : %s\n",
                     strerror(errno));
    close(config_fd);
    config_fd = -1;
}

/*
 * Consumes spaces and comments, but not the line feeds
 */
static void
consume_spaces(char *buf, size_t len, size_t *line, size_t *idx)
{
    (void)line;
    while (*idx < len && isspace(buf[*idx]))
    {
        if (buf[*idx] == '\n')
            break;
        ++(*idx);
    }
    if (*idx < len && buf[*idx] == '#')
    {
        while (*idx < len && buf[*idx] != '\n')
            ++(*idx);
    }
}

static bool
ini_span_empty_line(char *buf, size_t len, size_t *line, size_t *idx)
{
    size_t cur_idx = *idx;


    while (cur_idx < len && isspace(buf[cur_idx]))
    {
        if (buf[cur_idx] == '\n')
            break ;
        ++cur_idx;
    }
    // Return false if not EOF or not EOL
    if (cur_idx < len && buf[cur_idx] != '\n')
        return false;
    if (cur_idx == len)
        return false;
    if (cur_idx < len)
    {
        ++cur_idx;
        ++(*line);
    }
    *idx = cur_idx;
    return true;
}

/*
 * Reads a string, quoted or not, but stops at comments.
 */
static char*
read_string(char *buf, size_t len, size_t *line, size_t *idx)
{
    char        *string = NULL;
    bool        quoted = false;
    size_t      start_idx = *idx;
    size_t      end_idx = *idx;
    size_t      cur_idx = *idx;

    consume_spaces(buf, len, line, &cur_idx);
    start_idx = cur_idx;
    if (cur_idx < len && buf[cur_idx] == '"')
    {
        quoted = true;
        ++start_idx;
        ++cur_idx;
    }
    // Either it's quoted and we span until '"'
    // either it's not and :
    //  - No spaces
    //  - alphanums, '_', '-' and '.' are ok
    while ((quoted && !(buf[cur_idx] == '"' && buf[cur_idx - 1] != '\\'))
           || (!quoted && !isspace(buf[cur_idx])
               && (isalnum(buf[cur_idx]) || buf[cur_idx] == '.'
                   || buf[cur_idx] == '_' || buf[cur_idx] == '-')))
    {
        if (!quoted && buf[cur_idx] == '#')
            break ;
        ++cur_idx;
    }
    end_idx = cur_idx;
    if (quoted)
        ++cur_idx;

    // Then allocate string. (C99 _GNU_SOURCE for strndup)
    string = strndup(&buf[start_idx], end_idx - start_idx);

    *idx = cur_idx;
    return string;
}

static bool
ini_read_section(char *buf, size_t len, size_t *line,
                 size_t *idx, char **secname)
{
    size_t  cur_idx = *idx;

    consume_spaces(buf, len, line, &cur_idx);
    if (!(cur_idx < len && buf[cur_idx] == '['))
        return false;
    ++cur_idx;
    *secname = read_string(buf, len, line, &cur_idx);
    consume_spaces(buf, len, line, &cur_idx);
    if (!(cur_idx < len && buf[cur_idx] == ']'))
    {
        PRINTERR("[Loading Config]: no matching ']' line %i.\n", *line);
        free(*secname);
        *secname = NULL;
        return false;
    }
    ++cur_idx;
    if (ini_span_empty_line(buf, len, line, &cur_idx) == false)
    {
        free(*secname);
        *secname = NULL;
        PRINTERR("[Loading Config]: Junk at end of line %i.\n", *line);
        return false;
    }
    *idx = cur_idx;
    cloudmig_log(DEBUG_LVL,
                 "[Loading Config]: read section : %s...\n", *secname);
    return true;
}

static bool
ini_read_keyval(char *buf, size_t len, size_t *line,
                size_t *idx, char **key, char **val)
{
    size_t  cur_idx = *idx;

    consume_spaces(buf, len, line, &cur_idx);
    *key = read_string(buf, len, line, &cur_idx);
    consume_spaces(buf, len, line, &cur_idx);
    if (!(cur_idx < len && buf[cur_idx] == '='))
    {
        if (*key)
        {
            free(*key);
            *key = NULL;
        }
        return false;
    }
    ++cur_idx;
    if (*key == NULL || *key[0] == '\0')
    {
        PRINTERR("[Loading Config]: No key before character '=' in line %i.\n",
                 *line);
        if (*key)
            free(*key);
        *key = NULL;
        return false;
    }
    consume_spaces(buf, len, line, &cur_idx);
    *val = read_string(buf, len, line, &cur_idx);
    if (ini_span_empty_line(buf, len, line, &cur_idx) == false)
    {
        PRINTERR("[Loading Config]: Junk at end of line %i.\n", *line);
        return false;
    }
    cloudmig_log(DEBUG_LVL,
                 "[Loading Config]: read key/val : \"%s=%s\"...\n",
                 *key, *val);
    *idx = cur_idx;
    return true;
}

/*
 * For each option, the configuration file should overwrite the command-line
 * arguments settings. If the option is not "true" for the flags, then unset it
 */
static int
config_update_options(struct cldmig_config *conf,
                      char *section, char *key, char *val)
{
    if (strcasecmp(section, "source") == 0)
    {
        fprintf(src_profile, "%s=%s\n", key, val);
        fflush(src_profile);
        conf->src_entry_count += 1;
    }
    else if (strcasecmp(section, "destination") == 0)
    {
        fprintf(dst_profile, "%s=%s\n", key, val);
        fflush(dst_profile);
        conf->dst_entry_count += 1;
    }
    else if (strcasecmp(section, "cloudmig") == 0)
    {
        if (strcmp(key, "buckets") == 0)
        {
            opt_buckets(val);
        }
        else if (strcmp(key, "force-resume") == 0)
        {
            if (strcmp(val, "true") == 0)
                gl_options->flags |= RESUME_MIGRATION;
            else if (gl_options->flags & RESUME_MIGRATION)
                gl_options->flags ^= RESUME_MIGRATION;
        }
        else if (strcmp(key, "delete-source") == 0)
        {
            if (strcmp(val, "true") == 0)
                gl_options->flags |= DELETE_SOURCE_DATA;
            else if (gl_options->flags & DELETE_SOURCE_DATA)
                gl_options->flags ^= DELETE_SOURCE_DATA;
        }
        else if (strcmp(key, "background") == 0)
        {
            if (strcmp(val, "true") == 0)
                gl_options->flags |= BACKGROUND_MODE;
            else if (gl_options->flags & BACKGROUND_MODE)
                gl_options->flags ^= BACKGROUND_MODE;
        }
        else if (strcmp(key, "verbose") == 0)
            opt_verbose(val);
        else if (strcmp(key, "droplet-trace") == 0)
            opt_trace(val);
        else if (strcmp(key, "output") == 0)
        {
            if (val[0] != '\0')
            {
                char *file = strdup(val);
                if (file == NULL)
                {
                    PRINTERR("[Loading Config]: Could not set logfile %s\n",
                             strerror(errno));
                    return EXIT_FAILURE;
                }
                else
                    gl_options->logfile = file;
            }
        }
        else if (strcmp(key, "create-directories") == 0)
        {
            if (strcmp(val, "true") == 0)
                gl_options->flags |= AUTO_CREATE_DIRS;
            else if (gl_options->flags & AUTO_CREATE_DIRS)
                gl_options->flags ^= AUTO_CREATE_DIRS;
        }
    }
    else
        PRINTERR("[Loading Config]: Invalid section name '%s'.\n", section);
    return EXIT_SUCCESS;
}

static int
parse_config(char *buf, size_t len, struct cldmig_config *conf)
{
    int     ret = EXIT_SUCCESS;
    size_t  cur_idx = 0;
    size_t  line = 1;
    char    *secname = NULL;

    while (ini_read_section(buf, len, &line, &cur_idx, &secname)
           || ini_span_empty_line(buf, len, &line, &cur_idx))
    {
        // If null, then it was an empty line.
        if (secname == NULL)
            continue ;

        char    *key = NULL;
        char    *val = NULL;
        while (ini_read_keyval(buf, len, &line, &cur_idx, &key, &val)
               || ini_span_empty_line(buf, len, &line, &cur_idx))
        {
            // key valid only if ini_read_keyval successful.
            if (key)
            {
                if (config_update_options(conf,
                                          secname, key, val) == EXIT_FAILURE)
                    ret = EXIT_FAILURE;
                free(key);
                free(val);
                key = val = NULL;
            }
        }
        free(secname);
        secname = NULL;
    }
    if (cur_idx < len)
    {
        PRINTERR("[Loading Config]: Junk at end of file (line %i):"
                 " The values must be contained in a section.\n", line);
        return EXIT_FAILURE;
    }
    return ret;
}

int
load_config(struct cldmig_config *conf)
{
    char    *fbuf = NULL;
    size_t  fsize = 0;

    cloudmig_log(DEBUG_LVL,
                 "[Loading Config]: Starting configuration file parsing.\n");
    if (create_tmpfiles(conf->src_profile, conf->dst_profile,
                        sizeof(conf->src_profile),
                        sizeof(conf->dst_profile)) == EXIT_FAILURE)
    {
        PRINTERR("[Loading Config]: Could not create temporary profiles.\n", 0);
        goto err;
    }

    fbuf = map_config_file(gl_options->config, &fsize);
    if (fbuf == NULL)
        goto err;

    if (parse_config(fbuf, fsize, conf) == EXIT_FAILURE)
        goto err;

    unmap_config_file(fbuf, fsize);

    // All is well : set the right profiles for the droplet config loading.
    if (gl_options->flags & SRC_PROFILE_NAME)
        gl_options->flags ^= SRC_PROFILE_NAME;
    if (gl_options->flags & DEST_PROFILE_NAME)
        gl_options->flags ^= DEST_PROFILE_NAME;
    gl_options->src_profile = conf->src_profile;
    gl_options->dest_profile = conf->dst_profile;

    if (conf->src_entry_count == 0)
    {
        PRINTERR("[Loading Config]: No configuration for source profile!\n", 0);
        goto err;
    }
    if (conf->dst_entry_count == 0)
    {
        PRINTERR("[Loading Config]: No configuration for"
                 " destination profile!\n", 0);
        goto err;
    }
    if (cloudmig_options_check())
        goto err;

    return EXIT_SUCCESS;

err:
    if (src_profile != NULL)
        fclose(src_profile);
    if (dst_profile != NULL)
        fclose(dst_profile);
    if (fbuf)
        unmap_config_file(fbuf, fsize);
    return EXIT_FAILURE;
}

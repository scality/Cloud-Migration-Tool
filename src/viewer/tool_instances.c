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

#include <curses.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "viewer.h"
#include "tool_instances.h"

static int
get_transfer_description(char **desc, char *path)
{
    int ret = EXIT_FAILURE;
    int len = strlen(path);
    struct stat st;
    int cc = 0;
    char *buffer = NULL;
    int fd = -1;

    strcat(path, "/description.txt");

    if (stat(path, &st) == -1)
        goto err;

    fd = open(path, O_RDONLY);
    if (fd == -1)
        goto err;

    buffer = calloc(st.st_size+1, sizeof(*buffer));
    if (buffer == NULL)
        return ENOMEM; // Mark the fact that it is a memory matter.
    do
    {
        ret = read(fd, buffer+cc, st.st_size-cc);
        if (ret < 0)
        {
            if (errno == EAGAIN)
                continue ;
            goto err;
        }
        cc += ret;
    } while (cc < st.st_size && ret > 0);

    if (cc < st.st_size)
    {
        ret = EXIT_FAILURE;
        goto err;
    }

    *desc = buffer;
    buffer = NULL;

    ret = EXIT_SUCCESS;

err:
    path[len] = '\0';

    if (buffer)
        free(buffer);

    if (fd != -1)
        close(fd);

    return ret;
}

struct tool_instance*
get_instance_list(void)
{
    struct tool_instance    *b = NULL;
    DIR                     *cldmig_dir = NULL;
    struct dirent           *dir_entry = NULL;
    char                    path[512] = {0};

    cldmig_dir = opendir("/tmp/cloudmig");
    if (cldmig_dir == NULL)
        return NULL;

    while ((dir_entry = readdir(cldmig_dir)) != NULL)
    {
        path[0] = 0;
        strcpy(path, "/tmp/cloudmig/");
        path[15] = 0;
        strcat(path, dir_entry->d_name);
        // We only need to get directories which names are integers
        struct stat st;
        if (stat(path, &st) == -1
            || !S_ISDIR(st.st_mode))
            continue ; //Discard and go to next...

        // Then theres only numbers in the dir name.
        char*   desc = NULL;
        if (strlen(dir_entry->d_name)
            == strspn(dir_entry->d_name, "0123456789"))
        {
            int ret = get_transfer_description(&desc, path);
            if (ret == ENOMEM)
            {
                mvprintw(0, 0,
                "cloudmig-view: Could not allocate memory.\n");
                // Dont care about freeing allocated things,
                // we'll exit soon enough
                return NULL;
            }
            if (ret == EXIT_FAILURE)
                continue ; // Discard element and goto next.
            struct tool_instance *e = calloc(1, sizeof(*e));
            if (e == NULL)
            {
                mvprintw(0, 0,
                "cloudmig-view: Could not allocate memory.\n");
                // Dont care about freeing allocated things,
                // we'll exit soon enough
                return NULL;
            }
            e->dirpath = strdup(path);
            if (e->dirpath == NULL)
            {
                mvprintw(0, 0,
                "cloudmig-view: Could not allocate memory.\n");
                // Dont care about freeing allocated things,
                // we'll exit soon enough
                return NULL;
            }
            e->desc = desc; // the tiny description of the migration
            e->next = b;
            b = e;
        }
    }

    closedir(cldmig_dir);

    return b;
}

void
clear_instance_list(struct tool_instance *list)
{
    struct tool_instance *tmp;

    while (list)
    {
        tmp = list;
        list = list->next;
        free(tmp->desc);
        free(tmp->dirpath);
        free(tmp);
    }
}

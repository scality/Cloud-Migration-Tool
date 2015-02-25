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
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h> // unix socket

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "options.h"
#include "cloudmig.h"

#include "status_digest.h"
#include "status_store.h"
#include "display.h"
#include "viewer.h"

struct cldmig_display
{
    struct cloudmig_ctx     *ctx;

    pthread_t               thread;
    pthread_mutex_t         lock;
    int                     lock_inited;
    int                     stop;

    int                     accept_sock;
    char                    *sockfile;

    struct cldmig_viewer    *viewer;
};

static void
_display_lock(struct cldmig_display *disp)
{
    pthread_mutex_lock(&disp->lock);
}

static void
_display_unlock(struct cldmig_display *disp)
{
    pthread_mutex_unlock(&disp->lock);
}

static int
_display_create_accept_socket(char * filename)
{
    struct sockaddr_un  server_addr;
    int                 listen_fd = -1;

    bzero(&server_addr, sizeof(server_addr));
    server_addr.sun_family = AF_LOCAL;
    strcpy(server_addr.sun_path, filename);

    listen_fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (listen_fd == -1)
    {
        PRINTERR("Could not create listening socket for data display : %s.\n",
                 strerror(errno));
        return (-1);
    }

    if (bind(listen_fd, &server_addr, sizeof(server_addr)) == -1)
    {
        PRINTERR("Could not bind listening socket for data display : %s.\n",
                 strerror(errno));
        close(listen_fd);
        return (-1);
    }

    // The program should never have more than one simultaneous clients
    if (listen(listen_fd, 1) == -1)
    {
        PRINTERR("Could not listen to socket for data display : %s.\n",
                 strerror(errno));
        close(listen_fd);
        return (-1);
    }

    return listen_fd;
}

static void
_display_shutdown(struct cldmig_display *disp)
{
    if (disp->accept_sock != -1)
    {
        shutdown(disp->accept_sock, SHUT_RDWR);
        close(disp->accept_sock);
        disp->accept_sock = -1;
    }
}

static void*
_display_main_loop(struct cldmig_display *disp)
{

    struct timeval          timeout = {0, 0};
    int                     accept_fd = -1;
    fd_set                  rfds;
    int                     client_fd = -1;
    struct sockaddr_un      client_addr;
    socklen_t               client_socklen = sizeof(client_addr);

    _display_lock(disp);
    accept_fd = disp->accept_sock;
    while (!disp->stop)
    {
        _display_unlock(disp);

        FD_ZERO(&rfds);
        FD_SET(accept_fd, &rfds);
        if (select(accept_fd + 1, &rfds, NULL, NULL, &timeout) == -1)
        {
            if (errno != EINTR)
                PRINTERR("%s: Could not select anymore on listening socket: %s.\n",
                         __FUNCTION__, strerror(errno));
            goto relock;
        }

        // Client waiting for accept
        if (FD_ISSET(accept_fd, &rfds))
        {
            client_fd = accept(accept_fd,
                               (struct sockaddr*)&client_addr,
                               &client_socklen);
            if (client_fd == -1)
            {
                PRINTERR("%s: Could not accept viewer connection : %s.\n",
                         __FUNCTION__, strerror);
                goto relock;
            }

            _display_lock(disp);
            disp->viewer = viewer_create(disp->ctx, client_fd);
            _display_unlock(disp);

            if (disp->viewer)
            {
                viewer_run(disp->viewer);
                _display_lock(disp);
                viewer_destroy(disp->viewer);
                disp->viewer = NULL;
                _display_unlock(disp);
            }
            else
            {
                shutdown(client_fd, SHUT_RDWR);
                close(client_fd);
            }
        }

relock:
        _display_lock(disp);
    }
    _display_unlock(disp);

    return NULL;
}

void
display_trigger_update(struct cldmig_display *disp)
{
    if (disp->lock_inited)
    {
        _display_lock(disp);
        if (disp->viewer)
            viewer_trigger_update(disp->viewer);
        _display_unlock(disp);
    }
}

void
display_stop(struct cldmig_display *disp)
{
    int do_join = 0;

    if (disp->lock_inited)
    {
        _display_lock(disp);
        if (disp->stop == 0)
        {
            do_join = 1;
            disp->stop = 1;
            _display_shutdown(disp);
            if (disp->viewer)
                viewer_stop(disp->viewer);
        }
        _display_unlock(disp);
    }

    if (do_join)
        pthread_join(disp->thread, NULL);
}

void
display_destroy(struct cldmig_display *disp)
{
    char    path[64];

    display_stop(disp);

    if (disp->viewer)
    {
        viewer_stop(disp->viewer);
        viewer_destroy(disp->viewer);
        disp->viewer = NULL;
    }

    // For unlink, errors are not checked since other migrations may be running.
    if (disp->sockfile)
    {
        // Remove socket file
        unlink(disp->sockfile);

        // Remove description.txt
        disp->sockfile[strlen(disp->sockfile) - 12] = '\0'; // remove "display.sock"
        strcpy(path, disp->sockfile);
        strcat(path, "description.txt");
        unlink(path);

        // Remove parent dir (if possible)
        rmdir(disp->sockfile);
        rmdir("/tmp/cloudmig/");

        free(disp->sockfile);
        disp->sockfile = NULL;
    }

    free(disp);
}

struct cldmig_display*
display_create(struct cloudmig_ctx *ctx, char *src, char *dst)
{
    struct cldmig_display *ret = NULL;
    struct cldmig_display *disp = NULL;
    char    pid_str[32];
    char    desc_file[64];
    // By using the size of sun_path, we seek to avoid buffer overflows in file
    // names.
    struct sockaddr_un t;
    char    sockfile[sizeof(t.sun_path)];

    disp = calloc(1, sizeof(*disp));
    if (disp == NULL)
    {
        PRINTERR("Could not allocate display data.\n");
        goto end;
    }
    disp->ctx = ctx;
    disp->thread = -1;
    disp->stop = 1;
    disp->accept_sock = -1;
    disp->lock_inited = 0;

    if ((size_t)sprintf(pid_str, "/tmp/cloudmig/%hi", getpid()) > sizeof(pid_str))
    {
        PRINTERR("Could not compute rundir path: %s", strerror(errno));
        goto end;
    }

    if ((size_t)snprintf(sockfile, sizeof(sockfile),
                         "%s/display.sock", pid_str) > sizeof(sockfile))
    {
        PRINTERR("Could not compute display socket path: %s", strerror(errno));
        goto end;
    }

    disp->sockfile = strdup(sockfile);
    if (disp->sockfile == NULL)
        goto end;

    // Create the directories
    if (mkdir("/tmp/cloudmig", 0755) == -1)
    {
        if (errno != EEXIST)
        {
            PRINTERR("Could not create /tmp/cloudmig directory : %s.\n",
                     strerror(errno));
            goto end;
        }
    }

    if (mkdir(pid_str, 0755) == -1)
    {
        PRINTERR("Could not create %s directory : %s.\n",
                 pid_str, strerror(errno));
        goto end;
    }


    snprintf(desc_file, sizeof(desc_file), "%s/description.txt", pid_str);
    FILE* desc = fopen(desc_file, "w");
    if (desc == NULL)
        goto end;
    fprintf(desc, "%s to %s", src, dst);
    fclose(desc);

    disp->accept_sock = _display_create_accept_socket(sockfile);
    if (disp->accept_sock == -1)
        goto end;

    if (pthread_mutex_init(&disp->lock, NULL) == -1)
    {
        PRINTERR("Could not initialize display mutex.\n");
        goto end;
    }
    disp->lock_inited = 1;

    disp->stop = 0;
    if (pthread_create(&disp->thread, NULL,
                       (void*(*)(void*))_display_main_loop,
                       disp) == -1)
    {
        PRINTERR("Could not start display thread.\n");
        goto end;
    }

    ret = disp;
    disp = NULL;

end:
    if (disp)
        display_destroy(disp);

    return ret;
}

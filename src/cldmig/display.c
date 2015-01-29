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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "options.h"
#include "cloudmig.h"

#include "status_digest.h"
#include "status_store.h"


static int  unique_accept_sock = -1;
static char *unique_sockfile = NULL;


static int
create_log_socket(char * filename)
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

void
unsetup_var_pid_and_sock()
{
    char    path[64];
    shutdown(unique_accept_sock, SHUT_RDWR);
    close(unique_accept_sock);

    // For unlink, errors are not checked since other migrations may be running.
    unlink(unique_sockfile);
    unique_sockfile[strlen(unique_sockfile) - 12] = '\0'; // remove "display.sock"
    strcpy(path, unique_sockfile);
    strcat(path, "description.txt");
    unlink(path);
    rmdir(unique_sockfile);
    rmdir("/tmp/cloudmig/");
}

int
setup_var_pid_and_sock(char *src, char *dst)
{
    char    pid_str[32];
    char    desc_file[64];
    // By using the size of sun_path, we seek to avoid buffer overflows in file
    // names.
    struct sockaddr_un t;
    char    sockfile[sizeof(t.sun_path)];

    // Create the directory
    if (mkdir("/tmp/cloudmig", 0755) == -1)
    {
        if (errno != EEXIST)
        {
            PRINTERR("Could not create /tmp/cloudmig directory : %s.\n",
                     strerror(errno));
            return (EXIT_FAILURE);
        }
    }

    sprintf(pid_str, "/tmp/cloudmig/%hi", getpid());
    if (mkdir(pid_str, 0755) == -1)
    {
        PRINTERR("Could not create %s directory : %s.\n",
                 pid_str, strerror(errno));
        if (rmdir("/tmp/cloudmig") == -1)
        {
            if (errno != ENOTEMPTY)
            {
                PRINTERR("Could not remove /tmp/cloudmig directory : %s. \
                         Please remove it manually.\n",
                         strerror(errno));
            }
        }
        return (EXIT_FAILURE);
    }

    snprintf(desc_file, sizeof(desc_file), "%s/description.txt", pid_str);
    FILE* desc = fopen(desc_file, "w");
    if (desc == NULL)
        return EXIT_FAILURE;
    fprintf(desc, "%s to %s", src, dst);
    fclose(desc);

    snprintf(sockfile, sizeof(sockfile), "%s/display.sock",
             pid_str);
    // First, save the file name into a global string ptr.
    unique_sockfile = strdup(sockfile);
    if (unique_sockfile == NULL)
        return EXIT_FAILURE;

    unique_accept_sock = create_log_socket(sockfile);
    if (unique_accept_sock == -1)
        return (EXIT_FAILURE);

    return (EXIT_SUCCESS);
}


static void
accept_client(struct cloudmig_ctx *ctx)
{
    struct sockaddr_un      client_addr;
    socklen_t               client_socklen = sizeof(client_addr);

    int client_sock = accept(unique_accept_sock,
                             (struct sockaddr*)&client_addr,
                             &client_socklen);
    if (client_sock == -1)
    {
        PRINTERR("%s: Could not accept viewer connection : %s.\n",
                 __FUNCTION__, strerror);
        return ;
    }

    // If there's already a client, refuse, even thought it should not
    // have happened... (a .lock file should be present)
    if (ctx->viewer_fd != -1)
    {
        shutdown(client_sock, SHUT_RDWR);
        close(client_sock);
        return ;
    }

    // Store it in the ctx for ease of transmission
    ctx->viewer_fd = client_sock;
}


void
cloudmig_check_for_clients(struct cloudmig_ctx *ctx)
{
    struct timeval  timeout = {0, 0};
    fd_set          rfds;

    FD_ZERO(&rfds);
    FD_SET(unique_accept_sock, &rfds);

retry:
    if (select(unique_accept_sock + 1, &rfds, NULL, NULL, &timeout) == -1)
    {
        if (errno == EINTR)
            goto retry;
        PRINTERR("%s: Could not select on listening socket : %s.\n",
                 __FUNCTION__, strerror(errno));
        return ;
    }

    if (FD_ISSET(unique_accept_sock, &rfds))
        accept_client(ctx);
}


void
cloudmig_update_client(struct cloudmig_ctx *ctx)
{
    static struct timeval       lastupdate = {0, 0};
    struct timeval              tlimit = {0, 0};
    char                        head;

    // No use doing anything if there's no fd for it...
    if (ctx->viewer_fd == -1)
        return ;

    gettimeofday(&tlimit, NULL);
    // Update if :
    //  - transfer fully done
    //  OR
    //  - last update was more than 1/4 sec ago
    if (status_digest_get(ctx->status->digest, DIGEST_DONE_BYTES)
            != status_digest_get(ctx->status->digest, DIGEST_BYTES)
        && (((double)tlimit.tv_sec * 1000.
             + (double)tlimit.tv_usec/1000.)
            - ((double)lastupdate.tv_sec * 1000.
               + (double)lastupdate.tv_usec/1000.)) < 250)
        return ;
    lastupdate = tlimit; 

    /*
     * First, send the global information on the migration :
     */
    head = GLOBAL_INFO;
    // First write the header for viewer client
    if (send(ctx->viewer_fd, &head, 1, MSG_NOSIGNAL) == -1)
        goto unset_viewer_fd;

    // Then write the associated data
    // (unix socket, so don't care about endianness)
    if (send(ctx->viewer_fd, ctx->status->digest,
              sizeof(*ctx->status->digest), MSG_NOSIGNAL) == -1)
        goto unset_viewer_fd;

    /*
     * Next, send each thread info :
     */
    int threadid = -1;
    // Set the limit for removal of infolist items.
    tlimit.tv_sec -= 3;

    do
    {
        ++threadid;
        // TODO : Will have a loop here to iterate on all threads
        // XXX Lock the thread's info
        remove_old_items(
            &tlimit,
            (struct cldmig_transf**)&(ctx->tinfos[threadid].infolist)
        );

        struct cldmig_thread_info tinfo = {
            threadid,
            ctx->tinfos[threadid].fsize,
            ctx->tinfos[threadid].fdone,
            make_list_transfer_rate(
                (struct cldmig_transf*)(ctx->tinfos[threadid].infolist)
            ),
            strlen(ctx->tinfos[threadid].fpath) + 1
        };

        // Write the struct
        head = THREAD_INFO;
        if (send(ctx->viewer_fd, &head, 1, MSG_NOSIGNAL) == -1)
            goto unset_viewer_fd;
        if (send(ctx->viewer_fd, &tinfo, sizeof(tinfo), MSG_NOSIGNAL) == -1)
            goto unset_viewer_fd;
        // Write the filename
        if (send(ctx->viewer_fd,
              ctx->tinfos[threadid].fpath,
              tinfo.namlen, MSG_NOSIGNAL) == -1)
            goto unset_viewer_fd;
        // We're done with this thread !
        // XXX Unlock the thread's info
    } while ((threadid + 1) < ctx->options.nb_threads);

    return ;

unset_viewer_fd:
    if (errno == EPIPE)
    {
        close(ctx->viewer_fd);
        ctx->viewer_fd = -1;
    }
}




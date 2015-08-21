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
//

#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "display_protocol.h"
#include "data.h"
#include "viewer.h"

/*
 * this function connects to the unix local socket
 * and returns the file descriptor
 */
static int
connect_to_unix_socket(char *filepath)
{
    struct sockaddr_un  addr;
    int                 sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (sockfd == -1)
        return -1;

    bzero(&addr, sizeof(addr));
    addr.sun_family = AF_LOCAL;
    strcpy(addr.sun_path, filepath);
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
    {
        mvprintw(0,0, "socket could not connect : %s : %s!",
                 strerror(errno), filepath);
        refresh();
        sleep(1);
        close(sockfd);
        return -1;
    }

    return sockfd;
}

#include <stdarg.h>
static void
add_msg(struct message **msgs, char *format, ...)
{
    va_list         ap;
    struct message  *msg = NULL;
    struct message  *tmp = NULL;
    int             ret = 0;

    msg = calloc(1, sizeof(*msg));
    if (msg == NULL)
        return ;

    gettimeofday(&msg->tv, NULL);

    /*
     * Free entries older than 1 secs.
     */
    tmp = *msgs;
    while (tmp
           && (tmp->tv.tv_sec + 1 < msg->tv.tv_sec
               || (tmp->tv.tv_sec + 1 == msg->tv.tv_sec
                   && tmp->tv.tv_usec < msg->tv.tv_usec)))
    {
        *msgs = (*msgs)->next;
        free(tmp->msg);
        free(tmp);
        tmp = *msgs;
    }

    va_start(ap, format);
    msg->type = TEST;
    ret = vasprintf(&msg->msg, format, ap);
    va_end(ap);

    // It's only the viewer, so we can afford losing messages due to internal errors.
    if (ret < 0)
        goto end;

    if (*msgs)
    {
        struct message *last = *msgs;
        while (last->next != NULL)
            last = last->next;
        last->next = msg;
    }
    else
        *msgs = msg;
    msg = NULL;

end:
    if (msg)
    {
        if (msg->msg)
            free(msg->msg);
        free(msg);
    }
}

static int
state_machine_read(int sockfd, struct cldmig_global_info *ginfo,
                   struct thread_info **thr_data, uint32_t *thr_nb,
                   struct message **msgs)
{
    int     ret = EXIT_FAILURE;
    char    type = GLOBAL_INFO;

    // First read the msg type
    ret = read(sockfd, &type, 1);
    if (ret == -1)
        return EXIT_FAILURE;


    switch (type)
    {
    case GLOBAL_INFO:
        ret = read(sockfd, ginfo, sizeof(*ginfo));
        if (ret == -1)
            ret = EXIT_FAILURE;
        ret = EXIT_SUCCESS;
        break ;
    case THREAD_INFO:
        {
            uint32_t thr_id = -1;
            ret = read(sockfd, &thr_id, sizeof(thr_id));
            if (ret != sizeof(thr_id))
            {
                ret = EXIT_FAILURE;
                break ;
            }
            if (thr_id >= *thr_nb)
            {
                uint32_t n = *thr_nb;
                *thr_nb = thr_id + 1;
                *thr_data = realloc(*thr_data, (*thr_nb) * sizeof(**thr_data));
                // Let's bzero the newly allocated data
                bzero(*thr_data + n, ((*thr_nb) - n) * sizeof(**thr_data));
            }
            ret = read(sockfd, &(*thr_data)[thr_id], 4 * sizeof(uint32_t));
            if (ret != 4 * sizeof(uint32_t))
            {
                ret = EXIT_FAILURE;
                break ;
            }

            if ((*thr_data)[thr_id].name)
            {
                free((*thr_data)[thr_id].name);
                (*thr_data)[thr_id].name = NULL;
            }

            if ((*thr_data)[thr_id].fnamlen > 0)
            {
                (*thr_data)[thr_id].name = malloc((*thr_data)[thr_id].fnamlen + 1);
                if ((*thr_data)[thr_id].name == NULL)
                {
                    ret = EXIT_FAILURE;
                    break ;
                }
                ret = read(sockfd, (*thr_data)[thr_id].name, (*thr_data)[thr_id].fnamlen);
                if (ret != (int)((*thr_data)[thr_id].fnamlen))
                {
                    ret = EXIT_FAILURE;
                    break ;
                }
                (*thr_data)[thr_id].name[(*thr_data)[thr_id].fnamlen] = 0;
            }

            // Useful for debugging
            /*
            add_msg(msgs, "Read thread: name=%s formatted=%.*s\n",
                    (*thr_data)[thr_id].name,
                    (*thr_data)[thr_id].fnamlen,
                    (*thr_data)[thr_id].name);
            */

            ret = EXIT_SUCCESS;
        }
        break ;
    case MSG:
        {
            add_msg(msgs, "[DEBUG] Received message");
            ret = EXIT_SUCCESS;
        }
        break ;
    }

    return EXIT_SUCCESS;
}

static int
display_loop(int sockfd)
{
    int                 ret = EXIT_FAILURE;
    int                 maxfds = sockfd + 1;
    bool                stop = false;
    int                 c;
    fd_set              rfds;

    // Here the memory for the data received from main program
    struct cldmig_global_info   ginfos;
    uint32_t                    nb_threads = 1;
    struct thread_info          *thr_data = malloc(nb_threads * sizeof(struct thread_info));
    struct message              *msgs = NULL;
    
    memset(thr_data, 0, sizeof(*thr_data) * nb_threads);

    // First, clear screen...
    erase();

    do
    {
        FD_ZERO(&rfds);
        FD_SET(0, &rfds);
        FD_SET(sockfd, &rfds);
        ret = select(maxfds, &rfds, NULL, NULL, NULL);
        if (ret == -1)
        {
            // Interrupted by a signal, let's select again.
            if (errno == EINTR)
                continue ;
            goto err;
        }
        // Process the keyboard
        if (FD_ISSET(0, &rfds))
        {
            c = getch();
            if (c == 'q' || c == 'Q' || c == 9/*escape*/)
                stop = true;
        }
        // Network processing
        if (FD_ISSET(sockfd, &rfds))
        {
            state_machine_read(sockfd, &ginfos, &thr_data, &nb_threads, &msgs);
            if (display(&ginfos, thr_data, nb_threads, msgs) == EXIT_FAILURE)
                goto err;
        }
    } while (!stop);

    ret = EXIT_SUCCESS;

err:
    if (thr_data)
    {
        for (uint32_t i =0; i < nb_threads; ++i)
        {
            if (thr_data[i].name)
            {
                free(thr_data[i].name);
                thr_data[i].name = NULL;
            }
        }
        free(thr_data);
    }

    return ret;
}

int
view_instance(const char *path)
{
    int         ret = EXIT_FAILURE;
    int         lockfd = -1;
    int         sockfd = -1;
    char        *lockpath = NULL;
    char        *sockpath = NULL;

    lockpath = strdup(path);
    if (lockpath == NULL)
        goto failure;
    lockpath = realloc(lockpath, strlen(lockpath) + 15); // 15 for display.*
    if (lockpath == NULL)
        goto failure;

    strcat(lockpath, "/display.lock");

    // Create  a lock file to get control over socket...
    lockfd = open(lockpath, O_CREAT|O_EXCL, 0600);
    if (lockfd == -1)
        goto failure;

    /*
     * Now that the resource is kind of "locked", let's connect to it
     */
    sockpath = strdup(lockpath);
    if (sockpath == NULL)
        goto failure;
    // Change the name from /tmp/cloudmig/$PID/display.lock
    // to                   /tmp/cloudmig/$PID/display.sock
    sockpath[strlen(sockpath) - 4] = 's';

    // Now connect..
    sockfd = connect_to_unix_socket(sockpath);
    if (sockfd == -1)
        goto failure;


    display_loop(sockfd);



    // clean by removing socket lock.
    ret = EXIT_SUCCESS;

failure:
    if (sockfd != -1)
    {
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
    }
    if (sockpath)
        free(sockpath);

    if (lockfd != -1)
    {
        close(lockfd);
        unlink(lockpath);
    }
    if (lockpath != NULL)
        free(lockpath);

    return ret;
}

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

#include <pthread.h>

#include "cloudmig.h"
#include "status_digest.h"
#include "viewer.h"

struct cldmig_viewer
{
    struct cloudmig_ctx     *ctx;

    int                     stop;
    pthread_mutex_t         lock;
    int                     lock_inited;
    pthread_cond_t          cond;
    int                     cond_inited;

    int                     fd;
};


static void
_viewer_lock(struct cldmig_viewer *viewer)
{
    pthread_mutex_lock(&viewer->lock);
}

static void
_viewer_unlock(struct cldmig_viewer *viewer)
{
    pthread_mutex_unlock(&viewer->lock);
}

static void
_viewer_notify(struct cldmig_viewer *viewer)
{
    pthread_cond_signal(&viewer->cond);
}

static void
_viewer_shutdown(struct cldmig_viewer *viewer)
{
    if (viewer->fd != -1)
    {
        shutdown(viewer->fd, SHUT_RDWR);
        close(viewer->fd);
        viewer->fd = -1;
    }
}

static int
viewer_do_update(struct cldmig_viewer *viewer, struct timeval *tlimit)
{
    int                         ret;
    struct cloudmig_ctx         *ctx= viewer->ctx;
    struct cldmig_global_info   ginfo;
    struct cldmig_thread_info   tinfo;
    char                        *fpath = NULL;
    char                        head;

    /*
     * First, send the global information on the migration :
     */
    head = GLOBAL_INFO;
    // First write the header for viewer client
    if (send(viewer->fd, &head, 1, MSG_NOSIGNAL) == -1)
    {
        ret = -1;
        goto end;
    }

    ginfo.total_sz = status_digest_get(viewer->ctx->status->digest,
                                       DIGEST_BYTES);
    ginfo.done_sz = status_digest_get(viewer->ctx->status->digest,
                                      DIGEST_DONE_BYTES);
    ginfo.nb_objects = status_digest_get(viewer->ctx->status->digest,
                                         DIGEST_OBJECTS);
    ginfo.done_objects = status_digest_get(viewer->ctx->status->digest,
                                           DIGEST_DONE_OBJECTS);
    // Then write the associated data
    // (unix socket, so don't care about endianness)
    if (send(viewer->fd, &ginfo, sizeof(ginfo), MSG_NOSIGNAL) == -1)
    {
        ret = -1;
        goto end;
    }

    /*
     * Next, send each thread info :
     */
    int threadid = -1;

    do
    {
        ++threadid;

        {
            pthread_mutex_lock(&ctx->tinfos[threadid].lock);
            
            remove_old_items(
                tlimit,
                (struct cldmig_transf**)&(ctx->tinfos[threadid].infolist)
            );

            tinfo.id = threadid;
            tinfo.fsize = ctx->tinfos[threadid].fsize;
            tinfo.fdone = ctx->tinfos[threadid].fdone;
            tinfo.byterate = make_list_transfer_rate(
                    (struct cldmig_transf*)(ctx->tinfos[threadid].infolist)
                );
            tinfo.namlen = ctx->tinfos[threadid].fpath ?
                strlen(ctx->tinfos[threadid].fpath) + 1 : 0;

            fpath = NULL;
            if (ctx->tinfos[threadid].fpath)
                fpath = strdup(ctx->tinfos[threadid].fpath);

            pthread_mutex_unlock(&ctx->tinfos[threadid].lock);
        }

        if (fpath == NULL)
        {
            // Do not stop client for a ENOMEM -> do not return -1
            ret = 0;
            goto end;
        }

        // Write the struct
        head = THREAD_INFO;
        if (send(viewer->fd, &head, 1, MSG_NOSIGNAL) == -1)
        {
            ret = -1;
            goto end;
        }
        if (send(viewer->fd, &tinfo, sizeof(tinfo), MSG_NOSIGNAL) == -1)
        {
            ret = -1;
            goto end;
        }
        // Write the filename
        if (send(viewer->fd, fpath, tinfo.namlen, MSG_NOSIGNAL) == -1)
        {
            ret = -1;
            goto end;
        }

        free(fpath);
        fpath = NULL;
    } while ((threadid + 1) < ctx->options.nb_threads);

    ret = 0;

end:
    if (fpath)
        free(fpath);

    return ret;
}

void
viewer_run(struct cldmig_viewer *viewer)
{
    int                         ret;
    struct timespec             ts = {0, 0};
    struct timeval              tlimit = {0, 0};
    
    _viewer_lock(viewer);
    while (!viewer->stop)
    {
        // Add 0.250 secs for the update max delay
        if (tlimit.tv_usec > 750000)
        {
            tlimit.tv_sec += 1;
            tlimit.tv_usec = (tlimit.tv_usec + 250000) % 1000000;
        }
        else
            tlimit.tv_usec += 250000;

        // Wait notif OR timeout
        ts.tv_sec = tlimit.tv_sec;
        ts.tv_nsec = tlimit.tv_usec * 1000ull;
        ret = pthread_cond_timedwait(&viewer->cond, &viewer->lock, &ts);
        if (viewer->stop)
            break ;
        _viewer_unlock(viewer);

        gettimeofday(&tlimit, NULL);

        // Set the limit for removal of infolist items.
        tlimit.tv_sec -= CLOUDMIG_ETA_TIMEFRAME;

        ret = viewer_do_update(viewer, &tlimit);
        if (ret == -1)
            goto end;

        // back to current time.
        tlimit.tv_sec += CLOUDMIG_ETA_TIMEFRAME;

        _viewer_lock(viewer);
    }
    _viewer_unlock(viewer);

end:
    return ;
}

void
viewer_trigger_update(struct cldmig_viewer *viewer)
{
    _viewer_lock(viewer);
    _viewer_notify(viewer);
    _viewer_unlock(viewer);
}

void
viewer_stop(struct cldmig_viewer *viewer)
{
    if (viewer->lock_inited)
    {
        _viewer_lock(viewer);
        if (viewer->fd != -1 && viewer->stop == 0)
        {
            viewer->stop = 1;
            _viewer_notify(viewer);
        }
        _viewer_unlock(viewer);
    }
}

void
viewer_destroy(struct cldmig_viewer *viewer)
{
    _viewer_shutdown(viewer);

    if (viewer->lock_inited)
        pthread_mutex_destroy(&viewer->lock);
    if (viewer->cond_inited)
        pthread_cond_destroy(&viewer->cond);

    free(viewer);
}

struct cldmig_viewer*
viewer_create(struct cloudmig_ctx *ctx, int fd)
{
    struct cldmig_viewer    *ret = NULL;
    struct cldmig_viewer    *viewer = NULL;

    viewer = calloc(1, sizeof(*viewer));
    if (viewer == NULL)
    {
        PRINTERR("Could not allocate data for viewer.\n");
        goto end;
    }
    viewer->ctx = ctx;

    if (pthread_mutex_init(&viewer->lock, NULL) == -1)
    {
        PRINTERR("Could not intialize lock for viewer.\n");
        goto end;
    }
    viewer->lock_inited = 1;

    if (pthread_cond_init(&viewer->cond, NULL) == -1)
    {
        PRINTERR("Could not intialize cond for viewer.\n");
        goto end;
    }
    viewer->cond_inited = 1;

    viewer->fd = fd;

    ret = viewer;
    viewer = NULL;

end:
    if (viewer)
        viewer_destroy(viewer);

    return ret;
}

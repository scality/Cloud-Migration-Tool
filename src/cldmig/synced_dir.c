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

#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

#include "log.h"
#include "error.h"
#include "synced_dir.h"

static struct synceddir*    _sdir_new(struct synceddir_ctx *ctx, const char *path);
static void                 _sdir_delete(struct synceddir *sdir);
static void                 _sdir_notify(struct synceddir *sdir, bool exists);
static void                 _sdir_grab(struct synceddir *sdir);
static void                 _sdir_release(struct synceddir *sdir);

static struct synceddir*    _sdirlist_get(struct synceddir_ctx *ctx, const char *path);
static void                 _sdirlist_append(struct synceddir_ctx *ctx, struct synceddir *sdir);
static void                 _sdirlist_remove(struct synceddir_ctx *ctx, struct synceddir *sdir);

static void                 _sdirctx_lock(struct synceddir_ctx *ctx);
static void                 _sdirctx_unlock(struct synceddir_ctx *ctx);

struct synceddir*
_sdir_new(struct synceddir_ctx *ctx, const char *path)
{
    struct synceddir    *ret = NULL;
    struct synceddir    *sdir = NULL;
    char                *pathcpy = NULL;

    pathcpy = strdup(path);
    if (pathcpy == NULL)
    {
        PRINTERR(" Could not allocate path copy for new synchronized directory entry.");
        goto end;
    }

    sdir = calloc(1, sizeof(*sdir));
    if (sdir == NULL)
    {
        PRINTERR(" Could not allocate synchronized directory entry.");
        goto end;
    }
    sdir->ctx = ctx;

    if (pthread_cond_init(&sdir->notify_cond, NULL) == -1)
    {
        PRINTERR(" Could not intialize synchronized directory entry's condition.");
        goto end;
    }

    sdir->path = pathcpy;
    pathcpy = NULL;
    sdir->pathlen = strlen(sdir->path);
    sdir->exists = false;

    ret = sdir;
    sdir = NULL;

end:
    if (pathcpy)
        free(pathcpy);
    if (sdir)
        _sdir_delete(sdir);

    return ret;
}

static void
_sdir_delete(struct synceddir *sdir)
{
    assert(sdir->refcount == 0);

    if (sdir->ctx)
        _sdirlist_remove(sdir->ctx, sdir);

    pthread_cond_destroy(&sdir->notify_cond);
    free(sdir->path);
    free(sdir);
}

static void
_sdir_notify(struct synceddir *sdir, bool exists)
{
    sdir->done = true;
    sdir->exists = exists;
    if (sdir->refcount > 1)
        pthread_cond_broadcast(&sdir->notify_cond);
}

static void
_sdir_grab(struct synceddir *sdir)
{
    sdir->refcount += 1;
}

static void
_sdir_release(struct synceddir *sdir)
{
    assert(sdir->refcount > 0);
    sdir->refcount -= 1;
    if (sdir->refcount == 0)
        _sdir_delete(sdir);
}

static struct synceddir *
_sdirlist_get(struct synceddir_ctx *ctx, const char *path)
{
    struct synceddir *tmp = ctx->list.first;
    
    while (tmp != NULL && strcmp(path, tmp->path) != 0)
        tmp = tmp->next;

    return tmp;
}

static void
_sdirlist_append(struct synceddir_ctx *ctx, struct synceddir *sdir)
{
    sdir->prev = ctx->list.last;
    if (ctx->list.last)
        ctx->list.last->next = sdir;
    else
        ctx->list.first = sdir;
    ctx->list.last = sdir;
}

static void
_sdirlist_remove(struct synceddir_ctx *ctx, struct synceddir *sdir)
{
    struct synceddir    *prev = sdir->prev;
    struct synceddir    *next = sdir->next;

    if (prev)
        prev->next = next;
    else if (ctx->list.first == sdir)
        ctx->list.first = next;

    if (next)
        next->prev = prev;
    else if (ctx->list.last == sdir)
        ctx->list.last = prev;

    sdir->prev = NULL;
    sdir->next = NULL;
}

static void
_sdirctx_lock(struct synceddir_ctx *ctx)
{
    pthread_mutex_lock(&ctx->lock);
}

static void
_sdirctx_unlock(struct synceddir_ctx *ctx)
{
    pthread_mutex_unlock(&ctx->lock);
}

struct synceddir_ctx*
synced_dir_context_new(void)
{
    struct synceddir_ctx    *ret = NULL;
    struct synceddir_ctx    *ctx = NULL;

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        PRINTERR(" Could not allocate synchronized directory context.");
        goto end;
    }

    if (pthread_mutex_init(&ctx->lock, NULL) == -1)
    {
        PRINTERR(" Could not initialize synchronized directory context's lock.");
        goto end;
    }

    ret = ctx;
    ctx = NULL;

end:
    if (ctx)
        synced_dir_context_delete(ctx);

    return ret;
}

void
synced_dir_context_delete(struct synceddir_ctx *ctx)
{
    while (ctx->list.first)
    {
        struct synceddir *tmp = ctx->list.first;
        ctx->list.first = tmp->next;
        _sdir_delete(tmp);
    }
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
}

int
synced_dir_register(struct synceddir_ctx *ctx,
                    const char *path,
                    struct synceddir **sdirp, bool *is_responsiblep)
{
    int                 ret = EXIT_FAILURE;
    struct synceddir    *sdir = NULL;
    bool                responsible = false;

    _sdirctx_lock(ctx);

    sdir = _sdirlist_get(ctx, path);
    if (sdir == NULL)
    {
        responsible = true;

        sdir = _sdir_new(ctx, path);
        if (sdir == NULL)
            goto end;

        _sdirlist_append(ctx, sdir);
    }
    _sdir_grab(sdir);

    if (is_responsiblep)
        *is_responsiblep = responsible;

    *sdirp = sdir;

    ret = EXIT_SUCCESS;

end:
    _sdirctx_unlock(ctx);

    return ret;
}

void
synced_dir_unregister(struct synceddir *sdir, bool is_responsible, bool success)
{
    struct synceddir_ctx *ctx = sdir->ctx;

    _sdirctx_lock(ctx);
    
    if (is_responsible)
        _sdir_notify(sdir, success);

    _sdir_release(sdir);
    _sdirctx_unlock(ctx);
}

bool
synced_dir_completion_wait(struct synceddir *sdir)
{
    bool exists = false;

    _sdirctx_lock(sdir->ctx);
    if (!sdir->done)
        pthread_cond_wait(&sdir->notify_cond, &sdir->ctx->lock);
    exists = sdir->exists;
    _sdirctx_unlock(sdir->ctx);

    return exists;
}

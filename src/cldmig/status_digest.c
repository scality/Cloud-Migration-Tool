// Copyright (c) 2015, David Pineau
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

#include <unistd.h>

#include <droplet.h>
#include <droplet/vfs.h>

#include "cloudmig.h"
#include "status_digest.h"
#include "utils.h"

#define CLOUDMIG_STATUS_DIGEST_BYTES        "bytes"
#define CLOUDMIG_STATUS_DIGEST_DONE_BYTES   "done_bytes"
#define CLOUDMIG_STATUS_DIGEST_OBJECTS      "objects"
#define CLOUDMIG_STATUS_DIGEST_DONE_OBJECTS "done_objects"

static void
_digest_lock(struct status_digest *digest)
{
    pthread_mutex_lock(&digest->lock);
}

static void
_digest_unlock(struct status_digest *digest)
{
    pthread_mutex_unlock(&digest->lock);
}

struct status_digest*
status_digest_new(dpl_ctx_t *status_ctx, const char *storepath, uint64_t refresh_frequency)
{
    struct status_digest    *ret = NULL;
    struct status_digest    *digest = NULL;
    char                    *path = NULL;

    if (asprintf(&path, "%s/.cloudmig",
                 storepath) <= 0)
    {
        PRINTERR("[Loading Status Digest] Cannot allocate path string.\n");
        goto end;
    }

    digest = calloc(1, sizeof(*digest));
    if (digest == NULL)
    {
        PRINTERR("[Allocating Status Digest] Could not allocate status digest.\n");
        goto end;
    }

    if (pthread_mutex_init(&digest->lock, NULL) == -1)
    {
        PRINTERR("[Allocating Status Digest] Could not intialize lock.\n");
        goto end;
    }
    digest->lock_inited = 1;

    digest->path = path;
    path = NULL;
    
    digest->status_ctx = status_ctx;
    digest->refresh_frequency = refresh_frequency;

    ret = digest;
    digest = NULL;

end:
    if (path)
        free(path);
    if (digest)
        status_digest_free(digest);

    return ret;
}

void
status_digest_free(struct status_digest *digest)
{
    if (digest->path)
        free(digest->path);
    free(digest);
}

int
status_digest_download(struct status_digest *digest, int *regenerate)
{
    int                     ret;
    dpl_status_t            dplret;
    char                    *buffer = NULL;
    unsigned int            bufsize = 0;
    struct json_tokener     *tokener = NULL;
    struct json_object      *json = NULL;
    struct json_object      *field = NULL;
    uint64_t                bytes = 0;
    uint64_t                done_bytes = 0;
    uint64_t                objects = 0;
    uint64_t                done_objects = 0;

    *regenerate = 0;

    tokener = json_tokener_new();
    if (tokener == NULL)
    {
        PRINTERR("[Loading Status Digest] Could not allocate JSON tokener.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    dplret = dpl_fget(digest->status_ctx, digest->path,
                      NULL/*option*/, NULL/*condition*/, NULL/*range*/,
                      &buffer, &bufsize,
                      NULL/*MD*/, NULL/*sysmd*/);
    if (dplret != DPL_SUCCESS)
    {
        if (dplret == DPL_ENOENT)
        {
            *regenerate = 1;
            ret = EXIT_SUCCESS;
            goto end;
        }
        PRINTERR("[Loading Status Digest] Could not read status digest %s: %s.\n",
                 digest->path, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto end;
    }

    json = json_tokener_parse_ex(tokener, buffer, bufsize);
    if (json == NULL)
    {
        PRINTERR("[Loading Status Digest] Could not parse JSON.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    if (json_object_object_get_ex(json, CLOUDMIG_STATUS_DIGEST_BYTES, &field) == FALSE)
    {
        PRINTERR("[Loading Status Digest] "
                 "No field named '%s' in digest status JSON!\n",
                 CLOUDMIG_STATUS_DIGEST_BYTES);
        ret = EXIT_FAILURE;
        goto end;
    }
    bytes = json_object_get_int64(field);

    if (json_object_object_get_ex(json, CLOUDMIG_STATUS_DIGEST_DONE_BYTES, &field) == FALSE)
    {
        PRINTERR("[Loading Status Digest] "
                 "No field named '%s' in digest status JSON!\n",
                 CLOUDMIG_STATUS_DIGEST_DONE_BYTES);
        ret = EXIT_FAILURE;
        goto end;
    }
    done_bytes = json_object_get_int64(field);

    if (json_object_object_get_ex(json, CLOUDMIG_STATUS_DIGEST_OBJECTS, &field) == FALSE)
    {
        PRINTERR("[Loading Status Digest] "
                 "No field named '%s' in digest status JSON!\n",
                 CLOUDMIG_STATUS_DIGEST_OBJECTS);
        ret = EXIT_FAILURE;
        goto end;
    }
    objects = json_object_get_int64(field);

    if (json_object_object_get_ex(json, CLOUDMIG_STATUS_DIGEST_DONE_OBJECTS, &field) == FALSE)
    {
        PRINTERR("[Loading Status Digest] "
                 "No field named '%s' in digest status JSON!\n",
                 CLOUDMIG_STATUS_DIGEST_DONE_OBJECTS);
        ret = EXIT_FAILURE;
        goto end;
    }
    done_objects = json_object_get_int64(field);


    _digest_lock(digest);
    digest->fixed.objects = objects;
    digest->fixed.done_objects = done_objects;
    digest->fixed.bytes = bytes;
    digest->fixed.done_bytes = done_bytes;
    _digest_unlock(digest);

    ret = EXIT_SUCCESS;

end:
    if (buffer)
        free(buffer);
    if (json)
        json_object_put(json);

    return ret;
}

int
status_digest_upload(struct status_digest *digest)
{
    int                 ret;
    dpl_status_t        dplret;
    struct json_object  *json = NULL;
    struct json_object  *bytes = NULL;
    struct json_object  *done_bytes = NULL;
    struct json_object  *objects = NULL;
    struct json_object  *done_objects = NULL;
    const char          *filebuf = NULL;

    _digest_lock(digest);
    cloudmig_log(INFO_LVL, "Uploading digest: %lu/%lu objs, %lu/%lu bytes\n",
                 digest->fixed.done_objects, digest->fixed.objects,
                 digest->fixed.done_bytes, digest->fixed.bytes);
    json = json_object_new_object();
    bytes = json_object_new_int64(digest->fixed.bytes);
    done_bytes = json_object_new_int64(digest->fixed.done_bytes);
    objects = json_object_new_int64(digest->fixed.objects);
    done_objects = json_object_new_int64(digest->fixed.done_objects);
    _digest_unlock(digest);

    if (json == NULL || bytes == NULL || done_bytes == NULL
        || objects == NULL || done_objects == NULL)
    {
        PRINTERR("[Uploading Status Digest] "
                 "Could not allocate json items.\n");
        ret = EXIT_FAILURE;
        goto end;
    }
    json_object_object_add(json, CLOUDMIG_STATUS_DIGEST_OBJECTS, objects);
    json_object_object_add(json, CLOUDMIG_STATUS_DIGEST_DONE_OBJECTS, done_objects);
    json_object_object_add(json, CLOUDMIG_STATUS_DIGEST_BYTES, bytes);
    json_object_object_add(json, CLOUDMIG_STATUS_DIGEST_DONE_BYTES, done_bytes);
    objects = NULL;
    done_objects = NULL;
    bytes = NULL;
    done_bytes = NULL;

    filebuf = json_object_to_json_string(json);
    if (filebuf == NULL)
    {
        PRINTERR("[Uploading Status Digest] "
                 "Could not allocate JSON string representation.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    if ((dplret = dpl_fput(digest->status_ctx, digest->path,
                           NULL, NULL, NULL, // opt, cond, range
                           NULL, NULL, // md, sysmd
                           (char*)filebuf, strlen(filebuf))) != DPL_SUCCESS)
    {
        PRINTERR("[Uploading Status Digest] "
                 "Could not create digest status file : %s\n",
                 dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto end;
    }

    cloudmig_log(INFO_LVL, "[Uploading Status Digest] "
                 " Uploaded digest: %s\n", filebuf);

    ret = EXIT_SUCCESS;

end:
    if (json)
        json_object_put(json);
    if (bytes)
        json_object_put(bytes);
    if (done_bytes)
        json_object_put(done_bytes);
    if (objects)
        json_object_put(objects);
    if (done_objects)
        json_object_put(done_objects);

    return ret;
}

void
status_digest_delete(dpl_ctx_t *status_ctx, struct status_digest *digest)
{
    _digest_lock(digest);
    delete_file(status_ctx, "", digest->path);
    _digest_unlock(digest);
}

uint64_t
status_digest_get(struct status_digest *digest, enum digest_field field)
{
    uint64_t    value = 0;

    _digest_lock(digest);
    switch (field)
    {
    case DIGEST_OBJECTS:
        value = digest->fixed.objects;
        break ;
    case DIGEST_DONE_OBJECTS:
        value = digest->fixed.done_objects;
        break ;
    case DIGEST_BYTES:
        value = digest->fixed.bytes;
        break ;
    case DIGEST_DONE_BYTES:
        value = digest->fixed.done_bytes;
        break ;
    default:
        assert(0);
    }
    _digest_unlock(digest);

    return value;
}

void
status_digest_add(struct status_digest *digest,
                  enum digest_field field, uint64_t value)
{
    int     do_upload = 0;

    _digest_lock(digest);
    switch (field)
    {
    case DIGEST_OBJECTS:
        digest->fixed.objects += value;
        break ;
    case DIGEST_DONE_OBJECTS:
        digest->fixed.done_objects += value;
        digest->refresh_count += value;
        if (digest->refresh_count >= digest->refresh_frequency)
        {
            digest->refresh_count = 0;
            do_upload = 1;
        }
        break ;
    case DIGEST_BYTES:
        digest->fixed.bytes += value;
        break ;
    case DIGEST_DONE_BYTES:
        digest->fixed.done_bytes += value;
        break ;
    default:
        assert(0);
    }
    _digest_unlock(digest);

    if (do_upload)
        status_digest_upload(digest);
}

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

#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

#include <droplet.h>
#include <droplet/vfs.h>

#include "status.h"
#include "cloudmig.h"
#include "status_bucket.h"
#include "utils.h"

#define CLOUDMIG_STATUS_BUCKET_SRCPATH      "srcpath"
#define CLOUDMIG_STATUS_BUCKET_DSTPATH      "dstpath"
#define CLOUDMIG_STATUS_BUCKET_OBJSDONE     "objects_done"
#define CLOUDMIG_STATUS_BUCKET_N_OBJS       "objects_total"
#define CLOUDMIG_STATUS_BUCKET_BYTESDONE    "bytes_done"
#define CLOUDMIG_STATUS_BUCKET_N_BYTES      "bytes_total"
#define CLOUDMIG_STATUS_BUCKET_OBJECTS      "objects"

#define CLOUDMIG_STATUS_BUCKETENTRY_PATH    "path"
#define CLOUDMIG_STATUS_BUCKETENTRY_SIZE    "size"
#define CLOUDMIG_STATUS_BUCKETENTRY_TYPE    "type"
#define CLOUDMIG_STATUS_BUCKETENTRY_DONE    "done"

static void     _bucket_lock(struct bucket_status *bst);
static void     _bucket_unlock(struct bucket_status *bst);

static char*    _bucket_filepath(char *storepath, char *bucket_name);

static int      _bucket_add_entry(struct bucket_status *bckt,
                                  char *path, size_t size, dpl_ftype_t type);
static int      _bucket_set_paths(struct bucket_status *bckt, char *storepath,
                                  char *srcname, char *dstname);
static int      _bucket_set_infos(struct bucket_status *sbucket,
                                  uint64_t count, uint64_t size);
static int      _bucket_json_check(struct json_object *json_bucket,
                                   uint64_t *n_objsp, uint64_t *n_bytesp);


static void
_bucket_lock(struct bucket_status *bst)
{
    pthread_mutex_lock(&bst->lock);
}

static void
_bucket_unlock(struct bucket_status *bst)
{
    pthread_mutex_unlock(&bst->lock);
}

static char*
status_bucket_encodedname(const char *locator)
{
    char    *ret = NULL;
    char    *tmplocator = NULL;
    int     len = strlen(locator);

    // Need one "nul" byte to encode as the bucket name in the encoded form.
    if (locator[0] == ':')
    {
        tmplocator = calloc(len + 2, sizeof(*tmplocator));
        if (tmplocator == NULL)
            goto end;
        memcpy(&tmplocator[1], locator, len);
        len += 1;
        locator = tmplocator;
    }

    ret = cloudmig_urlencode(locator, len);
    if (ret == NULL)
        goto end;

end:
    if (tmplocator)
        free(tmplocator);

    return ret;
}

static char *
status_bucket_filename(const char *locator)
{
    char    *ret = NULL;
    char    *filename = NULL;
    char    *encoded = NULL;
    size_t  len;

    encoded = status_bucket_encodedname(locator);
    if (encoded == NULL)
        goto end;
    PRINTERR("name encoded as %s\n", encoded);

    len = strlen(encoded);
    filename = realloc(encoded, len + 6);
    if (filename == NULL)
        goto end;
    encoded = NULL;
    strcpy(&filename[len], ".json");

    PRINTERR("Encoded name(%s)=%s.\n",
             locator && locator[0] == 0 ? locator+1 : locator,
             filename);

    ret = filename;
    filename = NULL;

end:
    if (encoded)
        free(encoded);
    if (filename)
        free(filename);

    return ret;
}

static char*
_bucket_filepath(char *storepath, char *srcpath)
{
    char    *ret = NULL;
    char    *str = NULL;
    char    *name = NULL;

    name = status_bucket_filename(srcpath);
    if (name == NULL)
    {
        PRINTERR("Could not allocate bucket file name.\n");
        goto end;
    }

    if (asprintf(&str, "%s/%s", storepath, name) <= 0)
    {
        PRINTERR("Could not allocate bucket file path.\n");
        goto end;
    }

    cloudmig_log(INFO_LVL, "Computed bucket status path from name %s is %s\n", name, str);

    ret = str;
    str = NULL;

end:
    if (name)
        free(name);

    return ret;
}

int
status_bucket_namecmp(const char *encoded, const char *raw)
{
    char    *encraw = NULL;
    int     ret;

    encraw = status_bucket_filename(raw);
    if (encraw == NULL)
        goto end;

    ret = strcmp(encoded, encraw);

end:
    if (encraw)
        free(encraw);

    return ret;
}

static int
_bucket_add_entry(struct bucket_status *bckt,
                  char *path, size_t size, dpl_ftype_t type)
{
    int                 ret;
    int                 bucket_has_objs = 0;
    struct json_object  *objs = NULL;
    struct json_object  *obj = NULL;
    struct json_object  *jspath = NULL;
    struct json_object  *jssize = NULL;
    struct json_object  *jstype = NULL;
    struct json_object  *jsdone = NULL;

    cloudmig_log(DEBUG_LVL, "[Creating Bucket Status] "
                 "Adding entry path=%s size=%lu type=%i\n",
                 path, size, type);

    if (json_object_object_get_ex(bckt->json, CLOUDMIG_STATUS_BUCKET_OBJECTS,
                                  &objs) == TRUE)
        bucket_has_objs = 1;
    else
        objs = json_object_new_array();
    obj = json_object_new_object();
    jspath = json_object_new_string(path);
    jssize = json_object_new_int64(size);
    jstype = json_object_new_int((int)type);
    jsdone = json_object_new_boolean(FALSE);

    if (objs == NULL || obj == NULL
        || jspath == NULL || jssize == NULL
        || jstype == NULL || jsdone == NULL)
    {
        PRINTERR("[Creating Bucket Status] Could not allocate JSON objects.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    json_object_object_add(obj, CLOUDMIG_STATUS_BUCKETENTRY_PATH, jspath);
    json_object_object_add(obj, CLOUDMIG_STATUS_BUCKETENTRY_SIZE, jssize);
    json_object_object_add(obj, CLOUDMIG_STATUS_BUCKETENTRY_DONE, jsdone);
    json_object_object_add(obj, CLOUDMIG_STATUS_BUCKETENTRY_TYPE, jstype);
    jsdone = NULL;
    jspath = NULL;
    jssize = NULL;
    jstype = NULL;

    json_object_array_add(objs, obj);
    obj = NULL;

    if (bucket_has_objs == 0)
    {
        json_object_object_add(bckt->json,
                               CLOUDMIG_STATUS_BUCKET_OBJECTS, objs);
        objs = NULL;
    }

    ret = EXIT_SUCCESS;

end:
    if (obj)
        json_object_put(obj);
    if (objs && !bucket_has_objs)
        json_object_put(objs);
    if (jspath)
        json_object_put(jspath);
    if (jssize)
        json_object_put(jssize);
    if (jstype)
        json_object_put(jstype);
    if (jsdone)
        json_object_put(jsdone);

    return ret;
}

static int
_bucket_set_paths(struct bucket_status *bckt, char *storepath, char *srcname, char *dstname)
{
    int                     ret;
    struct json_object      *jsobj = NULL;
    struct json_object      *jssrc = NULL;
    struct json_object      *jsdst = NULL;
    char                    *fpath = NULL;

    fpath = _bucket_filepath(storepath, srcname);
    if (fpath == NULL)
    {
        PRINTERR("[Creating Status Bucket] "
                 "Could not allocate memory for status bucket file path.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    jssrc = json_object_new_string(srcname);
    if (jssrc == NULL)
    {
        PRINTERR("[Setting Bucket Status Path] "
                 "Could not allocate JSON string object.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    jsdst = json_object_new_string(dstname);
    if (jsdst == NULL)
    {
        PRINTERR("[Setting Bucket Status Path] "
                 "Could not allocate JSON string object.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    if (bckt->json == NULL)
    {
        jsobj = json_object_new_object();
        if (jsobj == NULL)
        {
            PRINTERR("[Setting Bucket Status Path] "
                     "Could not allocate JSON object.\n");
            ret = EXIT_FAILURE;
            goto end;
        }
    }


    if (bckt->json == NULL)
        json_object_object_add(jsobj, CLOUDMIG_STATUS_BUCKET_SRCPATH, jssrc);
    else
    {
        // Ensure old version is removed.
        json_object_object_del(bckt->json, CLOUDMIG_STATUS_BUCKET_SRCPATH);
        json_object_object_add(bckt->json, CLOUDMIG_STATUS_BUCKET_SRCPATH, jssrc);
    }
    jssrc = NULL;

    if (bckt->json == NULL)
        json_object_object_add(jsobj, CLOUDMIG_STATUS_BUCKET_DSTPATH, jsdst);
    else
    {
        // Ensure old version is removed.
        json_object_object_del(bckt->json, CLOUDMIG_STATUS_BUCKET_DSTPATH);
        json_object_object_add(bckt->json, CLOUDMIG_STATUS_BUCKET_DSTPATH, jsdst);
    }
    jsdst = NULL;

    if (bckt->json == NULL)
    {
        bckt->json = jsobj;
        jsobj = NULL;
    }

    if (bckt->path)
        free(bckt->path);
    bckt->path = fpath;
    fpath = NULL;

    cloudmig_log(DEBUG_LVL, "[Creating Bucket Status] "
                 "bucket paths set: path=%s\n", bckt->path);

    ret = EXIT_SUCCESS;

end:
    if (fpath)
        free(fpath);
    if (jssrc)
        json_object_put(jssrc);
    if (jsdst)
        json_object_put(jsdst);
    if (jsobj)
        json_object_put(jsobj);

    return ret;
}

static int
_bucket_set_infos(struct bucket_status *bst, uint64_t count, uint64_t size)
{
    int                     ret;
    struct json_object      *jszero = NULL;
    struct json_object      *jsnobjs = NULL;
    struct json_object      *jssize = NULL;

    jszero = json_object_new_int64(0);
    jsnobjs = json_object_new_int64(count);
    jssize = json_object_new_int64(size);
    if (jszero == NULL || jsnobjs == NULL || jssize == NULL)
    {
        PRINTERR("[Creating Bucket Status] Could not create JSON int.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    json_object_object_add(bst->json, CLOUDMIG_STATUS_BUCKET_OBJSDONE, json_object_get(jszero));
    json_object_object_add(bst->json, CLOUDMIG_STATUS_BUCKET_N_OBJS, jsnobjs);
    json_object_object_add(bst->json, CLOUDMIG_STATUS_BUCKET_BYTESDONE, jszero);
    json_object_object_add(bst->json, CLOUDMIG_STATUS_BUCKET_N_BYTES, jssize);
    jssize = NULL;
    jsnobjs = NULL;
    jszero = NULL;

    ret = EXIT_SUCCESS;

end:
    if (jszero)
        json_object_put(jszero);
    if (jsnobjs)
        json_object_put(jsnobjs);
    if (jssize)
        json_object_put(jssize);

    return ret;
}

/*
 * Value's true hidden type must depend on the expected json type:
 * - boolean -> int*
 * - int -> uint64_t*
 * - double -> double*
 * - string -> char **
 * - array -> struct json_object**
 * - object -> struct json_object**
 */
static int
_bucket_json_check_field(struct json_object *parent,
                         const char *name,
                         enum json_type expected_type,
                         void *value)
{
    int                 ret;
    struct json_object  *field = NULL;
    int                 *boolp = value;
    uint64_t            *u64p = value;
    double              *dp = value;
    const char          **strp = value;
    struct json_object  **objp = value;


    if (json_object_object_get_ex(parent, name, &field) == FALSE)
    {
        PRINTERR("[Loading Bucket Status] JSON does not contain "
                 "any field named '%s'.\n", name);
        ret = EXIT_FAILURE;
        goto end;
    }

    if (!json_object_is_type(field, expected_type))
    {
        PRINTERR("[Loading Bucket Status] JSON field '%s' does not have"
                 " the expected type: %s instead of %s.\n", name,
                 json_type_to_name(json_object_get_type(field)),
                 json_type_to_name(expected_type));
        ret = EXIT_FAILURE;
        goto end;
    }

    /*
     * Return the actual value
     */
    switch (expected_type)
    {
    case json_type_int:
        *u64p = json_object_get_int64(field);
        break ;
    case json_type_double:
        *dp = json_object_get_double(field);
        break ;
    case json_type_boolean:
        *boolp = json_object_get_boolean(field);
        break ;
    case json_type_string:
        *strp = json_object_get_string(field);
        if (strlen(*strp) == 0)
        {
            PRINTERR("[Loading Bucket Status] String field '%s' is empty.\n", name);
            ret = EXIT_FAILURE;
            goto end;
        }
        break ;
    case json_type_array:
    case json_type_object:
        *objp = field;
        break ;
    default:
        PRINTERR("[Loading Bucket Status] Unexpected JSON type: %s.\n",
                 json_type_to_name(expected_type));
        ret = EXIT_FAILURE;
        goto end;
    }

    ret = EXIT_SUCCESS;

end:
    return ret;
}

static int
_bucket_json_check(struct json_object *json_bucket,
                   uint64_t *n_objsp, uint64_t *n_bytesp)
{
    int                 ret;
    struct json_object  *objects = NULL;
    struct json_object  *obj = NULL;
    int64_t             n_objs = 0;
    uint64_t            fullsize = 0;
    uint64_t            entry_sz = 0;
    int                 entry_done = FALSE;
    int                 entry_type = 0;
    uint64_t            aggregated_size = 0;
    char                *str = NULL;

    /*
     * Here is the following json format we use:
     * Status
     *   -> srcpath (bucket source path)
     *   -> dstpath (bucket destination path)
     *   -> objects_done
     *   -> objects_total
     *   -> bytes_done
     *   -> bytes_total
     *   -> objects = [
     *        -> path (File path within bucket)
     *        -> Size
     *        -> Offset (Current amount of bytes migrated)
     *      ] (array of entries as described within brackets)
     */
    ret = _bucket_json_check_field(json_bucket, CLOUDMIG_STATUS_BUCKET_SRCPATH,
                                   json_type_string, (void*)&str);
    if (ret != EXIT_SUCCESS)
        goto end;

    ret = _bucket_json_check_field(json_bucket, CLOUDMIG_STATUS_BUCKET_DSTPATH,
                                   json_type_string, (void*)&str);
    if (ret != EXIT_SUCCESS)
        goto end;

    ret = _bucket_json_check_field(json_bucket, CLOUDMIG_STATUS_BUCKET_N_OBJS,
                                   json_type_int, (void*)&n_objs);
    if (ret != EXIT_SUCCESS)
        goto end;

    ret = _bucket_json_check_field(json_bucket, CLOUDMIG_STATUS_BUCKET_N_BYTES,
                                   json_type_int, (void*)&fullsize);
    if (ret != EXIT_SUCCESS)
        goto end;

    /* Check that the aggregation of sizes/n_objects is consistent with total fields */
    ret = _bucket_json_check_field(json_bucket, CLOUDMIG_STATUS_BUCKET_OBJECTS,
                                   json_type_array, (void*)&objects);
    if (ret != EXIT_SUCCESS)
        goto end;

    if (json_object_array_length(objects) != n_objs)
    {
        PRINTERR("[Loading Bucket Status] JSON Array does not contain"
                 " as many objects as expected: %i"
                 " for an 'objects_total' of %"PRIu64".\n",
                 json_object_array_length(objects), n_objs);
        ret = EXIT_FAILURE;
        goto end;
    }

    for (int i=0; i < n_objs; ++i)
    {
        obj = json_object_array_get_idx(objects, i);
        if (obj == NULL)
        {
            PRINTERR("[Loading Bucket Status] Could not retrieve "
                     "object at index %i of array.", i);
            ret = EXIT_FAILURE;
            goto end;
        }

        ret = _bucket_json_check_field(obj, CLOUDMIG_STATUS_BUCKETENTRY_PATH,
                                       json_type_string, (void*)&str);
        if (ret != EXIT_SUCCESS)
            goto end;

        ret = _bucket_json_check_field(obj, CLOUDMIG_STATUS_BUCKETENTRY_SIZE,
                                       json_type_int, (void*)&entry_sz);
        if (ret != EXIT_SUCCESS)
            goto end;

        ret = _bucket_json_check_field(obj, CLOUDMIG_STATUS_BUCKETENTRY_DONE,
                                       json_type_boolean, (void*)&entry_done);
        if (ret != EXIT_SUCCESS)
            goto end;

        ret = _bucket_json_check_field(obj, CLOUDMIG_STATUS_BUCKETENTRY_TYPE,
                                       json_type_int, (void*)&entry_type);
        if (ret != EXIT_SUCCESS)
            goto end;

        aggregated_size += entry_sz;
    }

    if (aggregated_size != fullsize)
    {
        PRINTERR("[Loading Bucket Status] Total size of bucket does not "
                 "match the aggregate of the size of the objects.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    if (n_objsp)
        *n_objsp = n_objs;
    if (n_bytesp)
        *n_bytesp = aggregated_size;

    ret = EXIT_SUCCESS;

end:
    return ret;
}

struct bucket_status*
status_bucket_new()
{
    struct bucket_status    *ret = NULL;
    struct bucket_status    *bst = NULL;

    bst = calloc(1, sizeof(*bst));
    if (bst == NULL)
    {
        PRINTERR("Could not allocate bucket status.\n");
        goto end;
    }

    if (pthread_mutex_init(&bst->lock, NULL) == -1)
        goto end;
    bst->lock_inited = 1;

    ret = bst;
    bst = NULL;

end:
    if (bst)
        status_bucket_free(bst);

    return ret;
}

void
status_bucket_free(struct bucket_status *bst)
{
    if (bst->json)
        json_object_put(bst->json);
    if (bst->path)
        free(bst->path);
    pthread_mutex_destroy(&bst->lock);

    free(bst);
}

int
status_bucket_dup_paths(struct bucket_status *bst,
                        char **statusp, char **srcp)
{
    int     ret = EXIT_FAILURE;
    bool    bucket_locked = false;
    char    *ststr = NULL;
    char    *srcstr = NULL;
    struct json_object  *obj = NULL;

    _bucket_lock(bst);
    bucket_locked = true;
    
    if (statusp)
        ststr = strdup(bst->path);

    if (srcp)
    {
        if (json_object_object_get_ex(bst->json,
                                      CLOUDMIG_STATUS_BUCKET_SRCPATH,
                                      &obj) == TRUE)
        {
            srcstr = strdup(json_object_get_string(obj));
        }
    }

    if ((statusp && ststr == NULL)
        || (srcp && srcstr == NULL))
    {
        PRINTERR("[Bucket Status DUP Paths] Could not duplicate strings.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    if (statusp)
    {
        *statusp = ststr;
        ststr = NULL;
    }
    if (srcp)
    {
        *srcp = srcstr;
        srcstr = NULL;
    }

    ret = EXIT_SUCCESS;

end:
    if (bucket_locked)
        _bucket_unlock(bst);
    if (ststr)
        free(ststr);
    if (srcstr)
        free(srcstr);

    return ret;
}

struct bucket_status*
status_bucket_load(dpl_ctx_t *status_ctx,
                   char *storepath, char *name,
                   uint64_t *countp, uint64_t *sizep)
{
    struct bucket_status    *ret = NULL;
    int                     iret;
    dpl_status_t            dplret;
    struct bucket_status    *sbucket = NULL;
    char                    *path = NULL;
    struct json_tokener     *tok = NULL;
    struct json_object      *obj = NULL;
    char                    *buffer = NULL;
    unsigned int            bufsize = 0;
    uint64_t                count = 0;
    uint64_t                size = 0;

    cloudmig_log(DEBUG_LVL, "[Loading Bucket Status] "
                 "Loading status for bucket from file %s/%s...\n", storepath, name);

    sbucket = status_bucket_new();
    if (sbucket == NULL)
        goto end;

    tok = json_tokener_new();
    if (tok == NULL)
    {
        PRINTERR("[Loading Bucket Status] Could not allocate JSON tokener.\n");
        goto end;
    }

    iret = asprintf(&path, "%s/%s", storepath, name);
    if (iret <= 0)
    {
        PRINTERR("[Loading Bucket Status] Could not allocate path.\n");
        goto end;
    }

    dplret = dpl_fget(status_ctx, path,
                      NULL/*option*/, NULL/*condition*/, NULL/*range*/,
                      &buffer, &bufsize,
                      NULL/*MD*/, NULL/*sysmd*/);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Loading Bucket Status] Could not get file: %s.\n",
                 dpl_status_str(dplret));
        goto end;
    }

    obj = json_tokener_parse_ex(tok, buffer, bufsize);
    if (obj == NULL)
    {
        PRINTERR("[Loading Bucket Status] Could not parse JSON.\n");
        goto end;
    }

    iret = _bucket_json_check(obj, &count, &size);
    if (iret != EXIT_SUCCESS)
    {
        PRINTERR("[Loading Bucket Status] Status for bucket %s seems erroneous.\n",
                 name);
        goto end;
    }

    sbucket->json = obj;
    obj = NULL;

    sbucket->path = path;
    path = NULL;

    ret = sbucket;
    sbucket = NULL;

    if (countp)
        *countp = count;
    if (sizep)
        *sizep = size;

    cloudmig_log(DEBUG_LVL, "[Loading Bucket Status] Loaded bucket status.\n");

end:
    if (buffer)
        free(buffer);
    if (tok)
        json_tokener_free(tok);
    if (path)
        free(path);
    if (obj)
        json_object_put(obj);

    return ret;
}

int 
_bucket_recurse(dpl_ctx_t *src_ctx,
                struct bucket_status *bst,
                char *dirpath,
                uint64_t    *added_countp,
                uint64_t    *added_sizep)
{
    int             ret;
    dpl_status_t    dplret;
    char            *subpath = NULL;
    void            *dir_hdl = NULL;
    dpl_dirent_t    dirent;
    uint64_t        added_count = 0;
    uint64_t        added_size = 0;
    char            *curpath = NULL;

    dplret = dpl_opendir(src_ctx, dirpath, &dir_hdl);
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Creating Bucket Status] Could not open directory %s: %s\n",
                 dirpath, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto end;
    }

    while (!dpl_eof(dir_hdl))
    {
        dplret = dpl_readdir(dir_hdl, &dirent);
        if (dplret != DPL_SUCCESS)
        {
            PRINTERR("[Creating Bucket Status] Could not read directory %s: %s\n",
                     dirpath, dpl_status_str(dplret));
            ret = EXIT_FAILURE;
            goto end;
        }

        ret = asprintf(&curpath, "%s%s", dirpath, dirent.name);
        if (ret <= 0)
        {
            PRINTERR("[Creating Bucket Status] "
                     "Could not allocate memory to compute full path.\n");
            ret = EXIT_FAILURE;
            goto end;
        }

        if (strcmp(dirent.name, ".") && strcmp(dirent.name, ".."))
        {
            ret = _bucket_add_entry(bst, curpath, dirent.size, dirent.type);
            if (ret != EXIT_SUCCESS)
            {
                ret = EXIT_FAILURE;
                goto end;
            }

            if (DPL_FTYPE_DIR == dirent.type)
            {
                dplret = _bucket_recurse(src_ctx, bst, curpath, &added_count, &added_size);
                if (dplret != DPL_SUCCESS)
                {
                    ret = EXIT_FAILURE;
                    goto end;
                }
            }

            added_count += 1;
            added_size += dirent.size;
        }

        free(curpath);
        curpath = NULL;
    }

    *added_countp += added_count;
    *added_sizep += added_size;

    ret = EXIT_SUCCESS;

end:
    if (dir_hdl)
        dpl_closedir(dir_hdl);
    if (subpath)
        free(subpath);
    if (curpath)
        free(curpath);

    return ret;
}

struct bucket_status*
status_bucket_create(dpl_ctx_t *status_ctx, dpl_ctx_t *src_ctx,
                     char *storepath, char *srcname, char *dstname,
                     uint64_t *countp, uint64_t *sizep)
{
    struct bucket_status    *ret = NULL;
    int                     iret;
    dpl_status_t            dplret = DPL_SUCCESS;
    struct bucket_status    *sbucket = NULL;
    uint64_t                added_count = 0;
    uint64_t                added_size = 0;
    char                    *bcktdir = NULL;
    // Bucket status' raw data
    const char              *filebuf = NULL;

    cloudmig_log(DEBUG_LVL, "[Creating Bucket Status] "
                 "Creating status file for bucket '%s'...\n", srcname);

    sbucket = status_bucket_new();
    if (sbucket == NULL)
        goto end;

    iret = _bucket_set_paths(sbucket, storepath, srcname, dstname);
    if (iret != EXIT_SUCCESS)
        goto end;

    iret = asprintf(&bcktdir, "%.*s", (int)(strlen(sbucket->path) - 5), sbucket->path);
    if (iret <= 0)
    {
        PRINTERR("[Creating Bucket Status] "
                 "Could not allocate memory for bucket dir path.\n");
        goto end;
    }

    iret = _bucket_recurse(src_ctx, sbucket, srcname, &added_count, &added_size);
    if (iret != EXIT_SUCCESS)
        goto end;

    iret = _bucket_set_infos(sbucket, added_count, added_size);
    if (iret != EXIT_SUCCESS)
        goto end;

    filebuf = json_object_to_json_string(sbucket->json);
    if (filebuf == NULL)
        goto end;

    if ((dplret = dpl_fput(status_ctx, sbucket->path,
                           NULL, NULL, NULL, // opt, cond, range
                           NULL, NULL, // md, sysmd
                           (char*)filebuf, strlen(filebuf))) != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not create bucket %s's status file at %s: %s\n",
                 __FUNCTION__, srcname, sbucket->path, dpl_status_str(dplret));
        goto end;
    }

    if ((dplret = dpl_mkdir(status_ctx, bcktdir, NULL/*MD*/, NULL/*sysmd*/)))
    {
        PRINTERR("[Creating Bucket Status] Could not mkdir '%s': %s.\n",
                 bcktdir, dpl_status_str(dplret));
        goto end;
    }

    cloudmig_log(DEBUG_LVL, "[Creating Bucket Status] Bucket %s: SUCCESS.\n",
                 srcname);

    *countp = added_count;
    *sizep = added_size;

    ret = sbucket;
    sbucket = NULL;

end:
    if (sbucket != NULL)
        status_bucket_free(sbucket);
    if (bcktdir)
        free(bcktdir);

    return ret;
}

void
status_bucket_delete(dpl_ctx_t *status_ctx, struct bucket_status *bst)
{
    _bucket_lock(bst);
    {
        char *dot = strrchr(bst->path, '.');
        *dot = 0;
        delete_directory(status_ctx, "Status Bucketdir", bst->path);
        *dot = '.';
    }
    delete_file(status_ctx, "Status Bucket", bst->path);
    _bucket_unlock(bst);
}

void
status_bucket_get(struct bucket_status *bst)
{
    _bucket_lock(bst);
    bst->refcount += 1;
    _bucket_unlock(bst);
}

void
status_bucket_release(struct bucket_status *bst)
{
    _bucket_lock(bst);
    assert(bst->refcount > 0);
    bst->refcount -= 1;
    _bucket_unlock(bst);
}

void
status_bucket_reset_iteration(struct bucket_status *bst)
{
    _bucket_lock(bst);
    bst->next_entry = 0;
    _bucket_unlock(bst);
}

static int
_bucket_entry_load(dpl_ctx_t *status_ctx, struct file_transfer_state *filestate)
{
    int                 ret = EXIT_FAILURE;
    dpl_status_t        dplret;
    char                *buffer = NULL;
    unsigned int        bufsize;
    struct json_tokener *tokener = NULL;
    struct json_object  *json = NULL;
    struct json_object  *srcstate = NULL;
    struct json_object  *dststate = NULL;
    struct json_object  *objoff = NULL;

    dplret = dpl_fget(status_ctx, filestate->status_path,
                      NULL/*option*/, NULL/*condition*/, NULL/*range*/,
                      &buffer, &bufsize,
                      NULL/*MD*/, NULL/*sysmd*/);
    if (dplret != DPL_SUCCESS)
    {
        if (dplret != DPL_ENOENT)
        {
            PRINTERR("[Bucket Status Loading Object] Could not get state file.\n");
            ret = EXIT_FAILURE;
            goto end;
        }

        // No intermediary state, not an error.
        ret = EXIT_SUCCESS;
        goto end;
    }

    tokener = json_tokener_new();
    if (tokener == NULL)
    {
        PRINTERR("[Bucket Status Loading Object] Could not allocate JSON tokener.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    json = json_tokener_parse_ex(tokener, buffer, bufsize);
    if (json == NULL)
    {
        PRINTERR("[Bucket Status Loading Object] Could not parse JSON.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    if (json_object_object_get_ex(json, "offset", &objoff) == FALSE)
    {
        PRINTERR("[Bucket Status Loading Object] Could find 'offset' field in JSON.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    if (json_object_object_get_ex(json, "rstatus", &srcstate) == FALSE)
    {
        PRINTERR("[Bucket Status Loading Object] Could find 'rstatus' field in JSON.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    if (json_object_object_get_ex(json, "wstatus", &dststate) == FALSE)
    {
        PRINTERR("[Bucket Status Loading Object] Could find 'wstatus' field in JSON.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    filestate->fixed.offset = json_object_get_int64(objoff);
    filestate->rstatus = json_object_get(srcstate);
    filestate->wstatus = json_object_get(dststate);

    ret = EXIT_SUCCESS;

end:
    if (buffer)
        free(buffer);
    if (json)
        json_object_put(json);

    return ret;
}


int
status_bucket_entry_update(dpl_ctx_t *status_ctx,
                           struct file_transfer_state *filestate)
{
    int                     ret;
    dpl_status_t            dplret;
    struct json_object      *json = NULL;
    struct json_object      *field = NULL;
    const char              *filebuf = NULL;

    json = json_object_new_object();
    if (json == NULL)
    {
        PRINTERR("[Bucket Status Entry Update] "
                 "Could not allocate json object.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    field = json_object_new_int64(filestate->fixed.offset);
    if (field == NULL)
    {
        PRINTERR("[Bucket Status Entry Update] "
                 "Could not allocate json object.\n");
        ret = EXIT_FAILURE;
        goto end;
    }
    json_object_object_add(json, "offset", field);
    field = NULL;

    json_object_object_add(json, "rstatus", json_object_get(filestate->rstatus));
    json_object_object_add(json, "wstatus", json_object_get(filestate->wstatus));

    filebuf = json_object_to_json_string(json);
    if (filebuf == NULL)
    {
        PRINTERR("[Bucket Status Entry Update] "
                 "Could not allocate json string representation.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    dplret = dpl_fput(status_ctx, filestate->status_path,
                      NULL/*options*/, NULL/*condition*/, NULL/*range*/,
                      NULL/*MD*/, NULL/*sysmd*/,
                      (char*)filebuf, strlen(filebuf));
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Bucket Status Entry Update] "
                 "Could not upload new JSON bucket status %s.\n",
                 filestate->status_path);
        ret = EXIT_FAILURE;
        goto end;
    }

    ret = EXIT_SUCCESS;

end:
    if (json)
        json_object_put(json);

    return ret;
}

int
status_bucket_entry_complete(dpl_ctx_t *status_ctx,
                             struct file_transfer_state *filestate)
{
    int                     ret;
    dpl_status_t            dplret;
    struct bucket_status    *bst = filestate->bst;
    bool                    bucket_locked = false;
    struct json_object      *objects = NULL;
    struct json_object      *object = NULL;
    struct json_object      *field = NULL;
    const char              *filebuf = NULL;

    cloudmig_log(DEBUG_LVL, "[Bucket Status Entry Complete] "
                 "Saving completion of object '%s'...\n",
                 filestate->obj_path);

    _bucket_lock(bst);
    bucket_locked = true;

    /*
     * Update json (in memory)
     */
    if (json_object_object_get_ex(bst->json, CLOUDMIG_STATUS_BUCKET_OBJECTS, &objects) == FALSE
        || !json_object_is_type(objects, json_type_array))
    {
        PRINTERR("[Bucket Status Entry Complete] "
                 "Bad JSON format, could not find '%s' array.\n",
                 CLOUDMIG_STATUS_BUCKET_OBJECTS);
        ret = EXIT_FAILURE;
        goto end;
    }

    object = json_object_array_get_idx(objects, filestate->state_idx);
    if (object == NULL)
    {
        PRINTERR("[Bucket Status Entry Complete] "
                 "Bad JSON format, could not find object for entry.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    field = json_object_new_boolean(TRUE);
    if (field == NULL)
    {
        PRINTERR("[Bucket Status Entry Complete] "
                 "Could not allocate new value.\n");
        ret = EXIT_FAILURE;
        goto end;
    }
    json_object_object_del(object, CLOUDMIG_STATUS_BUCKETENTRY_DONE);
    json_object_object_add(object, CLOUDMIG_STATUS_BUCKETENTRY_DONE, field);
    

    /*
     * Upload new json status.
     */
    filebuf = json_object_to_json_string(bst->json);
    if (filebuf == NULL)
    {
        PRINTERR("[Bucket Status Entry Complete] "
                 "Could not allocate json string representation.\n");
        ret = EXIT_FAILURE;
        goto end;
    }

    dplret = dpl_fput(status_ctx, bst->path,
                      NULL/*options*/, NULL/*condition*/, NULL/*range*/,
                      NULL/*MD*/, NULL/*sysmd*/,
                      (char*)filebuf, strlen(filebuf));
    if (dplret != DPL_SUCCESS)
    {
        PRINTERR("[Bucket Status Entry Complete] "
                 "Could not upload new JSON bucket status %s.\n",
                 bst->path);
        ret = EXIT_FAILURE;
        goto end;
    }

    _bucket_unlock(bst);
    bucket_locked = false;

    /*
     * Unlink temp status (if any)
     * We do it last, because if a previous operation fails,
     * we may still have a temp status; helping us avoid having to upload again.
     */
    dplret = dpl_unlink(status_ctx, filestate->status_path);
    if (dplret != DPL_SUCCESS)
    {
        if (dplret != DPL_ENOENT)
        {
            cloudmig_log(WARN_LVL, "[Bucket Status Entry Complete] "
                         "Could not delete the temp status file %s: %s",
                         filestate->status_path, dpl_status_str(dplret));
        }
    }

    ret = EXIT_SUCCESS;

end:
    if (bucket_locked)
        _bucket_unlock(bst);

    return ret;
}
static int
status_bucket_next_ex(dpl_ctx_t *status_ctx,
                      struct bucket_status *bst,
                      struct file_transfer_state *filestate,
                      int (*select)(uint64_t, bool),
                      int do_load)
{
    int                     ret;
    bool                    found = false;
    bool                    bucket_locked = false;
    unsigned int            cur_entry = 0;
    struct json_object      *objects = NULL;
    struct json_object      *obj = NULL;
    struct json_object      *objfield = NULL;
    uint64_t                n_objects = 0;
    dpl_ftype_t             objtype = DPL_FTYPE_UNDEF;
    uint64_t                objsize = 0;
    bool                    objdone = 0;
    const char              *objname = NULL;

    _bucket_lock(bst);
    bucket_locked = true;

    if (json_object_object_get_ex(bst->json,
                                  CLOUDMIG_STATUS_BUCKET_OBJECTS,
                                  &objects) == FALSE
        || !json_object_is_type(objects, json_type_array))
    {
        PRINTERR("[Bucket Status Next Entry] "
                 "Could not find object array within bucket's json status.\n");
        ret = -1;
        goto end;
    }
    n_objects = json_object_array_length(objects);
    
    /*
     * loop on the bucket state for each entry, until the end.
     * The loop automatically advances the next_entry index within the bucket
     * state descriptor.
     */
    for (; bst->next_entry < n_objects;)
    {
        obj = json_object_array_get_idx(objects, bst->next_entry);
        if (obj == NULL || !json_object_is_type(obj, json_type_object))
        {
            PRINTERR("[Bucket Status Next Entry] "
                     "Could not find object within bucket's json status: "
                     "Erroneous array index.\n");
            ret = -1;
            goto end;
        }

        if (json_object_object_get_ex(obj,
                                      CLOUDMIG_STATUS_BUCKETENTRY_SIZE,
                                      &objfield) == FALSE
            || !json_object_is_type(objfield, json_type_int))
        {
            PRINTERR("[Bucket Status Next Entry] "
                     "Could not find object '%s' within item's json status.\n",
                     CLOUDMIG_STATUS_BUCKETENTRY_SIZE);
            ret = -1;
            goto end;
        }
        objsize = json_object_get_int64(objfield);

        if (json_object_object_get_ex(obj,
                                      CLOUDMIG_STATUS_BUCKETENTRY_DONE,
                                      &objfield) == FALSE
            || !json_object_is_type(objfield, json_type_boolean))
        {
            PRINTERR("[Bucket Status Next Entry] "
                     "Could not find object '%s' within item's json status.\n",
                     CLOUDMIG_STATUS_BUCKETENTRY_DONE);
            ret = -1;
            goto end;
        }
        objdone = json_object_get_boolean(objfield);

        if (json_object_object_get_ex(obj,
                                      CLOUDMIG_STATUS_BUCKETENTRY_TYPE,
                                      &objfield) == FALSE
            || !json_object_is_type(objfield, json_type_int))
        {
            PRINTERR("[Bucket Status Next Entry] "
                     "Could not find object '%s' within item's json status.\n",
                     CLOUDMIG_STATUS_BUCKETENTRY_TYPE);
            ret = -1;
            goto end;
        }
        objtype = (dpl_ftype_t)json_object_get_int(objfield);

        if (json_object_object_get_ex(obj,
                                      CLOUDMIG_STATUS_BUCKETENTRY_PATH,
                                      &objfield) == FALSE
            || !json_object_is_type(objfield, json_type_string))
        {
            PRINTERR("[Bucket Status Next Entry] "
                     "Could not find object '%s' within item's json status.\n",
                     CLOUDMIG_STATUS_BUCKETENTRY_PATH);
            ret = -1;
            goto end;
        }
        objname = json_object_get_string(objfield);

        // We got all the pointers needed, advance next entry automatically.
        cur_entry = bst->next_entry;
        bst->next_entry += 1;

        /*
         * Check if this file has yet to be transfered
         *
         * Since the directories have a size of 0, we have to
         * store the transfered file's size +1, in order to identify
         * finished transfers and unfinished ones. Thus, the comparison to
         * know whether a file is transfered or not is offset <= size.
         */
        if (select(objsize, objdone))
        {
            found = true;

            /*
             * The path of each file is part of the status, so there is nothing
             * to compute for the paths.
             *
             * Then, try and load a possible saved state for those files (if any)
             */
            filestate->obj_path = strdup(objname);
            if (filestate->obj_path == NULL)
            {
                PRINTERR("[Bucket Status Next Entry] "
                         "Could not dup relative file path : %s.\n", strerror(errno));
                ret = -1;
                goto end;
            }

            // Compute state path
            if (asprintf(&filestate->status_path, "%.*s/%i.json",
                         (int)(strlen(bst->path) - 5), bst->path, cur_entry) == -1)
            {
                PRINTERR("[Bucket Status Next Entry] "
                         "Could not compute intermediary status file path: %s.\n",
                         strerror(errno));
                ret = -1;
                goto end;
            }

            // Unlock bucket to load intermediary status file
            _bucket_unlock(bst);
            bucket_locked = false;

            // Fill the filestate with the match found
            filestate->fixed.type = (uint32_t)objtype;
            filestate->fixed.size = objsize;
            filestate->fixed.offset = 0;
            filestate->state_idx = cur_entry;

            // Load intermediary status if flag set
            // (Adds additional info if upload was interrupted)
            if (do_load)
            {
                if (_bucket_entry_load(status_ctx, filestate) != EXIT_SUCCESS)
                {
                    ret = -1;
                    goto end;
                }
            }

            cloudmig_log(DEBUG_LVL, "[Bucket Status Next Entry]: "
                         "Next file: %s...\n", filestate->obj_path);

            break ;
        }

    }

    if (!found)
    {
        ret = 0;
    }
    else
    {
        bst->refcount += 1;
        filestate->bst = bst;
        ret = 1;
    }

end:
    if (bucket_locked)
        _bucket_unlock(bst);

    if (ret != 1)
    {
        if (filestate->obj_path)
            free(filestate->obj_path);
        filestate->obj_path = NULL;
        if (filestate->status_path)
            free(filestate->status_path);
        filestate->status_path = NULL;
    }

    return ret;
}

static int _bucket_entry_incomplete(uint64_t size, bool done) { (void)size; return !done; }

int
status_bucket_next_incomplete_entry(dpl_ctx_t *status_ctx,
                                    struct bucket_status *bst,
                                    struct file_transfer_state *filestate)
{
    return status_bucket_next_ex(status_ctx, bst, filestate,
                             &_bucket_entry_incomplete, 1);
}

static int _bucket_entry_all(uint64_t size, bool done) { (void)size; (void)done; return 1; }

int
status_bucket_next_entry(dpl_ctx_t *status_ctx,
                         struct bucket_status *bst,
                         struct file_transfer_state *filestate)
{
    return status_bucket_next_ex(status_ctx, bst, filestate,
                             &_bucket_entry_all, 0);
}

void
status_bucket_release_entry(struct file_transfer_state *filestate)
{
    _bucket_lock(filestate->bst);

    if (filestate->rstatus)
        json_object_put(filestate->rstatus);
    filestate->rstatus = NULL;

    if (filestate->wstatus)
        json_object_put(filestate->wstatus);
    filestate->wstatus = NULL;
    
    if (filestate->status_path)
        free(filestate->status_path);
    filestate->status_path = NULL;
    
    if (filestate->obj_path)
        free(filestate->obj_path);
    filestate->obj_path = NULL;

    filestate->bst->refcount -= 1;
    
    _bucket_unlock(filestate->bst);
    filestate->bst = NULL;
}


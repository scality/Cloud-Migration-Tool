#include <errno.h>
#include <string.h>

#include "cloudmig.h"

/*
 *
 * This file contains a collection of functions used to manipulate
 * the status file of the transfer.
 *
 */

/*
 * Computes the name of the bucket used to store the transfer status files
 * depending on the destination and source, in order to avoid any bucket name
 * conflict.
 *
 * This function returns a buffer of allocated memory that has to be freed
 */
static char*     compute_status_bucket(struct cloudmig_ctx* ctx)
{
    // Those should not be invalid.
    assert(ctx);
    assert(ctx->src_ctx);
    assert(ctx->src_ctx->host);
    assert(ctx->dest_ctx);
    assert(ctx->dest_ctx->host);

    char* name = 0;
    int len = 0;
    /*
     * result string will be of the form :
     * "cloudmig_srchostname_to_desthostname"
     */
    len = 9 + strlen(ctx->src_ctx->host) + 4 + strlen(ctx->dest_ctx->host);
    if (len < 255)
    {
        if ((name = calloc(sizeof(*name), len + 1)) == NULL)
        {
            PRINTERR("%s: Could not compute status bucket name : %s\n",
                     __FUNCTION__, strerror(errno));
            return (NULL);
        }
        strcpy(name, "cloudmig.");
        strcat(name, ctx->src_ctx->host);
        strcat(name, ".to.");
        strcat(name, ctx->dest_ctx->host);
    }
    else
        return (strdup("cloudmig.status"));
    return (name);
}

static int create_status_bucket(struct cloudmig_ctx* ctx)
{
    assert(ctx != NULL);
    assert(ctx->status.bucket != NULL);

    dpl_status_t    ret;

    ret = dpl_make_bucket(ctx->src_ctx, ctx->status.bucket,
                          DPL_LOCATION_CONSTRAINT_US_STANDARD,
                          DPL_CANNED_ACL_PRIVATE);
    if (ret != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not create status bucket '%s' (%i bytes): %s\n",
                 __FUNCTION__, ctx->status.bucket, strlen(ctx->status.bucket),
                 dpl_status_str(ret));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int create_status_file(struct cloudmig_ctx* ctx, dpl_bucket_t* bucket)
{
    assert(ctx != NULL);
    assert(ctx->src_ctx != NULL);
    assert(bucket != NULL);

    dpl_status_t    dplret = DPL_SUCCESS;
    dpl_vec_t*      objects = NULL;

    if ((dplret = dpl_list_bucket(ctx->src_ctx, bucket->name,
                                  NULL, NULL, &objects, NULL)) != DPL_SUCCESS)
    {
        PRINTERR("%s: Could not list bucket %s : %s\n", __FUNCTION__,
                 bucket->name, dpl_status_str(dplret));
        dpl_vec_objects_free(objects);
        return EXIT_FAILURE;
    }

    /*
     * For each file,  make a put_buffered request to create
     * the status file step by step.
     */
    dpl_object_t** cur_object = (dpl_object_t**)objects->array;
    cloudmig_log(DEBUG_LVL, "[Creating status] Bucket %s (%i objects):\n",
                 bucket->name, objects->n_items);
    for (int i = 0; i < objects->n_items; ++i, ++cur_object)
    {
        cloudmig_log(DEBUG_LVL,
                     "[Creating status] \t file : '%s'(%i bytes)\n",
                     (*cur_object)->key, (*cur_object)->size);
    }


    // Now that's done, free the memory allocated by the libdroplet
    dpl_vec_objects_free(objects);
    return EXIT_SUCCESS;
}


int load_status(struct cloudmig_ctx* ctx)
{
    assert(ctx);
    int             ret = EXIT_SUCCESS;
    dpl_status_t    dplret;
    int             resuming = 0; // Used to differentiate resuming migration from starting it

    // First, make sure we have a status bucket defined.
    if (ctx->status.bucket == NULL)
        ctx->status.bucket = compute_status_bucket(ctx);
    if (ctx->status.bucket == NULL)
        return (EXIT_FAILURE);

    /*
     * Now retrieve the bucket list for the source account
     * in order to check if the status bucket already exists or not
     *
     * By the way, we do not care about the destination's bucket list,
     * since the user should know that the tool makes a forceful copy.
     */
    dpl_vec_t*  src_buckets = 0;
    if ((dplret = dpl_list_all_my_buckets(ctx->src_ctx,
                                       &src_buckets)) != DPL_SUCCESS)
    {
        
        PRINTERR("%s: Could not list source's buckets : %s\n",
                 __FUNCTION__, dpl_status_str(dplret));
        ret = EXIT_FAILURE;
        goto free_status_name;
    }
    for (int i = 0; i < src_buckets->n_items; ++i)
    {
        if (strcmp(((dpl_bucket_t**)(src_buckets->array))[i]->name,
                   ctx->status.bucket) == 0)
        {
            cloudmig_log(DEBUG_LVL, "Found status bucket (%s) on source storage\n",
                         ctx->status.bucket);
            resuming = 1;
            break ;
        }
    }
    if (resuming == 0)// Then we must build the new status file through
    {
        // First, create the status bucket since it didn't exist.
        if ((ret = create_status_bucket(ctx)))
        {
            PRINTERR("%s: Could not create status bucket\n", __FUNCTION__);
            goto free_buckets_vec;
        }

        /*
         * For each bucket, we create a file named after it in the
         * status bucket.
         */
        dpl_bucket_t**  cur_bucket = (dpl_bucket_t**)src_buckets->array;
        for (int i = 0; i < src_buckets->n_items; ++i, ++cur_bucket)
        {
            if ((ret = create_status_file(ctx, *cur_bucket)))
            {
                cloudmig_log(WARN_LVL,
                             "An Error happened while creating the status bucket and file.\nPlease delete manually the bucket '%s' before restarting the tool...\n",
                             ctx->status.bucket);
                goto free_buckets_vec;
            }
        }
    }

    // Now, set where we are going to start/resume the migration from




free_buckets_vec:
    dpl_vec_buckets_free(src_buckets);

free_status_name:
    free(ctx->status.bucket);
    ctx->status.bucket = NULL;

    return ret;
}

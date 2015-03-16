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

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cloudmig.h"
#include "options.h"
#include "status_store.h"
#include "status_digest.h"
#include "display.h"
#include "synced_dir.h"


enum cloudmig_loglevel  gl_loglevel = INFO_LVL;
bool                    gl_isbackground = false;

static struct cloudmig_ctx     *gl_ctx = NULL;

void
cloudmig_sighandler(int sig)
{
    switch (sig)
    {
    case SIGINT:
        if (gl_ctx)
        {
            cloudmig_log(INFO_LVL, "Interrupted by SINGINT... stopping.\n");
            migration_stop(gl_ctx);
        }
        break ;
    default:
        break;
    }
}

int main(int argc, char* argv[])
{
    int                     ret = EXIT_FAILURE;
    time_t                  starttime = 0;
    time_t                  difftime = 0;
    struct cloudmig_ctx     ctx = CTX_INITIALIZER;
    struct sigaction        signal_action;
    // hosts strings for source and destination
    char	            *src_hostname = NULL;
    char	            *dst_hostname = NULL;
    dpl_status_t	    dplret = DPL_FAILURE;

    gl_ctx = &ctx;

    // set stderr as logger for the time being...
    cloudmig_openlog(NULL);

    // Initializations for the main program.
    starttime = time(NULL);

    // Retrieve options and parse config (if need be)
    if (retrieve_opts(&ctx.options, argc, argv) == EXIT_FAILURE)
        return (EXIT_FAILURE);

    // Load a json configuration file
    if (ctx.options.config)
    {
        if (load_config(&ctx.config, &ctx.options) == EXIT_FAILURE)
            return (EXIT_FAILURE);
    }

    // Setup the background mode if need be.
    if (gl_isbackground)
    {
        int pid = fork();
        if (pid == -1)
            PRINTERR("Could not initiate background mode : %s\n",
                     strerror(errno));
        if (pid != 0) // The father quits.
            return EXIT_SUCCESS;
    }

    // Start the main program : Setup log, load profiles, and start migration.
    cloudmig_openlog(ctx.options.logfile);

    ctx.tinfos = calloc(ctx.options.nb_threads, sizeof(*ctx.tinfos));
    if (ctx.tinfos == NULL)
    {
        PRINTERR("Could not allocate memory for transfer statuses.");
        return (EXIT_FAILURE);
    }

    // Copy Read-Only config data to each thread info to avoid the need
    // to manage concurrent accesses.
    for (int i=0; i < ctx.options.nb_threads; i++)
    {
        ctx.tinfos[i].ctx = &ctx;
        ctx.tinfos[i].config_flags = ctx.options.flags;
        ctx.tinfos[i].stop = true;
        if (pthread_mutex_init(&ctx.tinfos[i].lock, NULL) == -1)
        {
            PRINTERR("Could not initialize thread %i's mutex.", i);
            goto failure;
        }
        ctx.tinfos[i].lock_inited = 1;
    }

    ctx.synced_dir_ctx = synced_dir_context_new();

    // Allocate/Initialize the two droplet contexts
    if (load_profiles(&ctx) == EXIT_FAILURE)
        goto failure;

    // Retrieve hosts strings for source/dest
    {
        dpl_addr_t  *addr = NULL;
	dplret = dpl_addrlist_get_nth(ctx.src_ctx->addrlist, 0, &addr);
	if (dplret != DPL_SUCCESS && dplret != DPL_ENOENT)
        {
            PRINTERR("Could not retrieve host from the source addrlist", 0);
            goto failure;
	}
        src_hostname = strdup(ret == DPL_ENOENT || addr == NULL ? "local_posix" : addr->host);
        if (src_hostname == NULL)
        {
            PRINTERR("Could not retrieve host from the source addrlist: "
                     "strdup failed", 0);
            goto failure;
        }

	dplret = dpl_addrlist_get_nth(ctx.dest_ctx->addrlist, 0, &addr);
	if (dplret != DPL_SUCCESS && dplret != DPL_ENOENT)
        {
            PRINTERR("Could not retrieve host from the dest addrlist", 0);
            goto failure;
	}
        dst_hostname = strdup(ret == DPL_ENOENT || addr == NULL ? "local_posix" : addr->host);
        if (dst_hostname == NULL)
        {
            PRINTERR("Could not retrieve host from the dest addrlist: "
                     "strdup failed", 0);
            goto failure;
        }
    }

    ctx.display = display_create(&ctx, src_hostname, dst_hostname);
    if (ctx.display == NULL)
        goto failure;

    ctx.status = status_store_new();
    if (ctx.status == NULL)
        goto failure;

    if (status_store_load(&ctx, src_hostname, dst_hostname) == EXIT_FAILURE)
        goto failure;

    uint64_t done_objects = status_digest_get(ctx.status->digest, DIGEST_DONE_OBJECTS);
    uint64_t done_bytes = status_digest_get(ctx.status->digest, DIGEST_DONE_BYTES);

    /* Setup the signal catching before starting the migration */
    signal_action.sa_handler = &cloudmig_sighandler;
    sigemptyset(&signal_action.sa_mask);
    signal_action.sa_flags = 0;
    sigaction(SIGINT, &signal_action, NULL);

    ret = migrate(&ctx);
    if (ret != EXIT_SUCCESS)
        goto failure;

    // Migration ended : Now we can display a status for the migration session.
    difftime = time(NULL);
    difftime -= starttime;

    done_objects = status_digest_get(ctx.status->digest, DIGEST_DONE_OBJECTS) - done_objects;
    done_bytes = status_digest_get(ctx.status->digest, DIGEST_DONE_BYTES) - done_bytes;

    cloudmig_log(STATUS_LVL,
        "End of data migration. During this session :\n"
        "\tTransfered %llu objects, totaling %llu/%llu objects.\n"
        "\tTransfered %llu Bytes, totaling %llu/%llu Bytes.\n"
        "\tAverage transfer speed : %llu Bytes/s.\n"
        "\tTransfer Duration : %ud%uh%um%us.\n",
        done_objects,
        status_digest_get(ctx.status->digest, DIGEST_DONE_OBJECTS),
        status_digest_get(ctx.status->digest, DIGEST_OBJECTS),
        done_bytes,
        status_digest_get(ctx.status->digest, DIGEST_DONE_BYTES),
        status_digest_get(ctx.status->digest, DIGEST_BYTES),
        difftime == 0 ? 0 : done_bytes / difftime,
        difftime / (60 * 60 * 24),
        difftime / (60 * 60) % 24,
        difftime / 60 % 60,
        difftime % 60
    );

failure:
    if (ctx.options.config)
    {
        // Delete both temp files
        unlink(ctx.config.src_profile);
        unlink(ctx.config.dst_profile);
        // replace ".profile" by ".csv"
        strcpy(strrchr(ctx.config.src_profile, '.'), ".csv");
        strcpy(strrchr(ctx.config.dst_profile, '.'), ".csv");
        // Delete .csv files generated by libdroplet.
        unlink(ctx.config.src_profile);
        unlink(ctx.config.dst_profile);
    }
    if (ctx.display)
        display_destroy(ctx.display);

    if (src_hostname)
	free(src_hostname);
    if (dst_hostname)
	free(dst_hostname);

    if (ctx.status)
        status_store_free(ctx.status);
    if (ctx.tinfos)
        free(ctx.tinfos);
    if (ctx.src_ctx)
        dpl_ctx_free(ctx.src_ctx);
    if (ctx.dest_ctx)
        dpl_ctx_free(ctx.dest_ctx);
    if (ctx.status_ctx)
        dpl_ctx_free(ctx.status_ctx);
    if (ctx.synced_dir_ctx)
        synced_dir_context_delete(ctx.synced_dir_ctx);
    if (ctx.options.src_buckets)
    {
        for (int i=0; i < ctx.options.n_buckets; i++)
            free(ctx.options.src_buckets[i]);
        free(ctx.options.src_buckets);
    }
    if (ctx.options.dst_buckets)
    {
        for (int i=0; i < ctx.options.n_buckets; i++)
            free(ctx.options.dst_buckets[i]);
        free(ctx.options.dst_buckets);
    }
    cloudmig_closelog();

    return ret;
}

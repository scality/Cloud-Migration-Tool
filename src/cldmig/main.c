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
#include <stdlib.h>
#include <string.h>

#include "cloudmig.h"
#include "options.h"


struct cloudmig_options *	gl_options = NULL;
int                         gl_accept_sock = -1;
char                        *gl_sockfile = NULL;


void
cloudmig_sighandler(int sig)
{
    switch (sig)
    {
    case SIGINT:
        cloudmig_log(INFO_LVL, "Interrupted by SINGINT... stopping.\n");
        unsetup_var_pid_and_sock();
        cloudmig_closelog();
        exit(EXIT_SUCCESS);
        break ;
    default:
        break;
    }
}

int main(int argc, char* argv[])
{
    int     ret = EXIT_FAILURE;
    time_t  starttime = 0;
    time_t  difftime = 0;
    struct cldmig_config conf = CONF_INITIALIZER;
    struct cloudmig_options options = OPTIONS_INITIALIZER;
    // hosts strings for source and destination
    char	*src_hostname = NULL;
    char	*dst_hostname = NULL;
    dpl_status_t	dplret = DPL_FAILURE;

    cloudmig_openlog(NULL); // set stderr as logger for the time being...

    // Initializations for the main program.
    gl_options = &options;
    starttime = time(NULL);

    // Retrieve options and parse config (if need be)
    if (retrieve_opts(argc, argv) == EXIT_FAILURE)
        return (EXIT_FAILURE);

    if (gl_options->config)
    {
        if (load_config(&conf) == EXIT_FAILURE)
            return (EXIT_FAILURE);
    }

    // Setup the background mode if need be.
    if (options.flags & BACKGROUND_MODE)
    {
        int pid = fork();
        if (pid == -1)
            PRINTERR(": Could not initiate background mode : %s\n",
                     strerror(errno));
        if (pid != 0) // The father quits.
            return EXIT_SUCCESS;
    }

    // Start the main program : Setup log, load profiles, and start migration.
    cloudmig_openlog(options.logfile);

    struct cloudmig_ctx     ctx = CTX_INITIALIZER;
    ctx.tinfos = calloc(options.nb_threads, sizeof(*ctx.tinfos));
    if (ctx.tinfos == NULL)
    {
        PRINTERR("Could not allocate memory for transfer informations.", 0);
        return (EXIT_FAILURE);
    }

    if (load_profiles(&ctx) == EXIT_FAILURE)
        goto failure;

    // Retrieve hosts strings for source/dest
    {
	dplret = dpl_addrlist_get_nth(ctx.src_ctx->addrlist,
				      ctx.src_ctx->cur_host,
				      &src_hostname, NULL, NULL, NULL);
	if (dplret != DPL_SUCCESS)
	{
	    src_hostname = strdup("local_posix");
	    if (dplret != DPL_ENOENT || src_hostname == NULL)
	    {
		PRINTERR("Could not retrieve host from the source addrlist", 0);
		goto failure;
	    }
	}

	dplret = dpl_addrlist_get_nth(ctx.dest_ctx->addrlist,
				      ctx.dest_ctx->cur_host,
				      &dst_hostname, NULL, NULL, NULL);
	if (dplret != DPL_SUCCESS)
	{
	    dst_hostname = strdup("local_posix");
	    if (dplret != DPL_ENOENT || dst_hostname == NULL)
	    {
		PRINTERR("Could not retrieve host from the dest addrlist", 0);
		goto failure;
	    }
	}
    }
    if (setup_var_pid_and_sock(src_hostname,
                               dst_hostname) == EXIT_FAILURE)
        return (EXIT_FAILURE);

    if (load_status(&ctx, src_hostname, dst_hostname) == EXIT_FAILURE)
        goto failure;

    struct cloudmig_ctx ref = ctx;

    signal(SIGINT, &cloudmig_sighandler);

    if (migrate(&ctx) == EXIT_FAILURE)
        goto failure;


    // Migration ended : Now we can display a status for the migration session.
    difftime = time(NULL);
    difftime -= starttime;

    ref.status.general.head.done_objects = ctx.status.general.head.done_objects
        - ref.status.general.head.done_objects;
    ref.status.general.head.done_sz = ctx.status.general.head.done_sz
        - ref.status.general.head.done_sz;
    cloudmig_log(STATUS_LVL, "End of data migration. During this session :\n"
        "Transfered %llu/%llu objects\n"
        "Transfered %llu/%llu Bytes\n"
        "Average transfer speed : %llu Bytes/s\n"
        "Transfer Duration : %ud%uh%um%us\n",
        ref.status.general.head.done_objects,
        ref.status.general.head.nb_objects,
        ref.status.general.head.done_sz,
        ref.status.general.head.total_sz,
        difftime == 0 ? 0 : ref.status.general.head.done_sz / difftime,
        difftime / (60 * 60 * 24),
        difftime / (60 * 60) % 24,
        difftime / 60 % 60,
        difftime % 60
    );

	ret = EXIT_SUCCESS;

failure:
    if (gl_options->config)
    {
        // Delete both temp files
        unlink(conf.src_profile);
        unlink(conf.dst_profile);
        // replace ".profile" by ".csv"
        strcpy(strrchr(conf.src_profile, '.'), ".csv");
        strcpy(strrchr(conf.dst_profile, '.'), ".csv");
        // Delete .csv files generated by libdropler.
        unlink(conf.src_profile);
        unlink(conf.dst_profile);
    }
    if (ctx.src_ctx)
        unsetup_var_pid_and_sock();
    if (src_hostname)
	free(src_hostname);
    if (dst_hostname)
	free(dst_hostname);
    return (ret);
}

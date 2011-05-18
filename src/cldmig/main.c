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

#include <signal.h>
#include <stdlib.h>

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
        printf("Interrupted by SINGINT... stopping.\n");
        unsetup_var_pid_and_sock();
        exit(EXIT_SUCCESS);
        break ;
    default:
        break;
    }
}

int main(int argc, char* argv[])
{
	struct cloudmig_options options = {0, 0, 0, 0, 0, 0, 1};
	gl_options = &options;

    if (retrieve_opts(argc, argv) == EXIT_FAILURE)
        return (EXIT_FAILURE);

    if (setup_var_pid_and_sock() == EXIT_FAILURE)
        return (EXIT_FAILURE);
	
    struct cloudmig_ctx     ctx =
        {
            NULL,
            NULL,
            -1,
            { { {0, 0, 0, 0}, 0, NULL }, NULL, 0, 0, NULL},
            NULL
        };
    ctx.tinfos = calloc(options.nb_threads, sizeof(*ctx.tinfos));
    if (ctx.tinfos == NULL)
    {
        PRINTERR("Could not allocate memory for transfer informations.", 0);
        goto failure;
    }

    if (load_profiles(&ctx) == EXIT_FAILURE)
        goto failure;

    if (load_status(&ctx) == EXIT_FAILURE)
        goto failure;

    signal(SIGINT, &cloudmig_sighandler);

    if (migrate(&ctx) == EXIT_FAILURE)
        goto failure;

	return (EXIT_SUCCESS);

failure:
    unsetup_var_pid_and_sock();


	return (EXIT_FAILURE);
}

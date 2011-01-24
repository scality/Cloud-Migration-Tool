#include <stdlib.h>

#include "cloudmig.h"
#include "options.h"

struct cloudmig_options *	gl_options = NULL;

int main(int argc, char* argv[])
{
	struct cloudmig_options options = {0, 0, 0, 0, 0};
	gl_options = &options;

    if (retrieve_opts(argc, argv))
        return (EXIT_FAILURE);
	
	return (EXIT_SUCCESS);
}

#include <stdlib.h>
#include "flux_api_mockup.h"

int main (int argc, char *argv[])
{
    flux_rc_e rc;
    flux_lwj_id_t lwj;

    if ( ( rc = FLUX_init () != FLUX_OK )) {
	error_log ("Test Failed: "
	    "FLUX_init failed.");
	return EXIT_FAILURE;
    }

    if ( ( rc = FLUX_query_pid2LWJId (FLUX_MOCKUP_HOSTNAME,
				      FLUX_MOCKUP_PID,
				      &lwj) ) != FLUX_OK ) {
	error_log ("Test Failed: "
	    "FLUX_query_pid2LWJId returned an error.");
	return EXIT_FAILURE;
    }

    if (lwj.id == FLUX_MOCKUP_LWJ_ID) {
	error_log ("Test Passed");
    }
    else {
	error_log ("Test Failed: "
	    "FLUX_query_pid2LWJId returned an incorrect lwj id.");
	return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#include <stdlib.h>
#include <string.h>
#include "flux_api_mockup.h"

int main (int argc, char *argv[])
{
    flux_rc_e rc;
    flux_lwj_id_t lwj;
    flux_lwj_info_t lwjInfo;

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

    if ( ( rc = FLUX_query_LWJId2JobInfo (&lwj, 
					  &lwjInfo) ) != FLUX_OK ) {
	error_log ("Test Failed: "
	    "FLUX_query_LWJId2JobInfo returned an error.");
	
	return EXIT_FAILURE;
    }

    if ( (lwjInfo.status != FLUX_MOCKUP_STATUS)
	 || (strcmp (lwjInfo.hn, FLUX_MOCKUP_HOSTNAME) != 0)
	 || (lwjInfo.pid != FLUX_MOCKUP_PID)
	 || (lwjInfo.procTableSize != 1)
	 || (lwjInfo.procTable == NULL) ) {
	
	error_log ("Test Failed: "
	    "FLUX_query_LWJId2JobInfo returned incorrect info.");
	
	return EXIT_FAILURE;
    }
    else {
        error_log ("Test Passed");
    }

    return EXIT_SUCCESS;
}

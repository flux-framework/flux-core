#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "flux_api_mockup.h"

#define NNODES 1
#define NPROCS 4

int main (int argc, char *argv[])
{
    flux_rc_e rc;
    flux_lwj_id_t lwj;
    flux_lwj_info_t lwjInfo;
    int status;
    int sync = 0;
    
    if ( ( rc = FLUX_init () != FLUX_OK )) {
	error_log ("Test Failed: "
	    "FLUX_init failed.");
	return EXIT_FAILURE;
    }

    if (argc > 2) {
	error_log ("Usage: test_launch_spawn [sync]");

	return EXIT_FAILURE;
    }

    if ( (argc == 2 )
	 && (strcmp (argv[1], "sync") == 0) ) {
	sync = 1;
    }
    else {
	error_log ("Test Warning: "
	    "sync flag is not understood. Ignore %s", 
	    argv[1]);	
    }

    if ( ( rc = FLUX_update_createLWJCxt(&lwj)) 
	 != FLUX_OK ) {
	error_log ("Test Failed: "
	    "FLUX_update_createLWJCx returned an error.");

	return EXIT_FAILURE;
    }

    if (lwj.id != (FLUX_MOCKUP_LWJ_ID+1)) {
	error_log ("Test Failed: "
	    "LWJ id incorrect.");

	return EXIT_FAILURE;
    }

    if ( ( rc = FLUX_query_LWJId2JobInfo (
                    &lwj, &lwjInfo) ) != FLUX_OK ) {
	error_log ("Test Failed: "
	    "FLUX_query_LWJId2JobInfo returned an error.");
	
	return EXIT_FAILURE;
    }

    char * const lwjargv[2] = {"./test_sleeper", NULL};
    if ( ( rc = FLUX_launch_spawn (&lwj,sync,NULL, 
				   "./test_sleeper", lwjargv,
				   0,NNODES,NPROCS)) != FLUX_OK ) {
	error_log ("Test Failed: "
	    "FLUX_launch_spawn returned an error.");
	
	return EXIT_FAILURE;
    }

    sleep(2);
    
    if ( ( rc = FLUX_query_LWJStatus (&lwj, &status)) != FLUX_OK ) {
	error_log ("Test Failed: "
	    "FLUX_query_LWJStatus returned an error.");
	
	return EXIT_FAILURE;
    }
    
    if ( ( (sync == 0) 
	   && (status != status_spawned_running))
	|| ( (sync == 1)
	     && (status != status_spawned_stopped)) ) {
	error_log ("Test Failed: "
	    "FLUX_query_LWJStatus returned an incorrect status.");
	
	return EXIT_FAILURE;
    }

    size_t size;
    if ( ( rc = FLUX_query_globalProcTableSize (&lwj, &size)) 
	 != FLUX_OK ) {
	error_log ("Test Failed: "
	    "FLUX_query_globalProcTableSize returned an error.");
	
	return EXIT_FAILURE;
    }

    MPIR_PROCDESC_EXT *proctable 
	= (MPIR_PROCDESC_EXT *) 
	  malloc (size * sizeof (MPIR_PROCDESC_EXT));

    if ( ( rc = FLUX_query_globalProcTable (&lwj, 
		    proctable, size)) != FLUX_OK ) {
	error_log ("Test Failed: "
	    "FLUX_query_globalProcTable returned an error.");
	
	return EXIT_FAILURE;
    }

    int i;
    for (i=0; i < size; ++i) {
	error_log ("=====================================");
	error_log ("executable: %s", proctable[i].pd.executable_name);
	error_log ("hostname: %s", proctable[i].pd.host_name);
	error_log ("pid: %d", proctable[i].pd.pid);
	error_log ("mpirank: %d", proctable[i].mpirank);
	error_log ("cnodeid: %d", proctable[i].cnodeid);
    }
    error_log ("=====================================");

    if (sync == 1) {
	system ("ps x");
	for (i=0; i < size; ++i) {
	    kill (proctable[i].pd.pid, SIGCONT);
	}	
	error_log ("=====================================");
	system ("ps x");	
    }

    if (size == NPROCS) {
	error_log ("Test Passed");
    }
    else {
	error_log ("Test Failed");
	return EXIT_FAILURE;	
    }

    return EXIT_SUCCESS;
}

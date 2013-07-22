/* mp_cobo_fen.c - exercise public interfaces */

#include "mp_util.h"
#include "cobo.h"

int 
main (int argc, char *argv[])
{
    int p             = 0;
    int h             = 0;
    int rc            = 0;
    int numProcs      = -1;
    size_t lLength    = 0;
    int *portList     = NULL;
    char **hostList   = NULL;
    char *hLFileName  = NULL;
    char *aLine       = NULL;   
    FILE *fptr        = NULL;
    char cmdBuf[PATH_MAX] = {0};

    if (argc != 3) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "Usage: FrontEnd hostListFile numProcesses");
	exit(1);
    }

    numProcs = atoi(argv[2]);
    hLFileName = argv[1];

    if ( access (hLFileName, R_OK) != -1 ) {
	if ( remove (hLFileName) == -1 ) {
	    mp_cobo_say_msg (COBOOUT_PREFIX, 1,
	        "Remove %s failed: %s", 
		hLFileName,
	        strerror(errno) );
	    exit(1);	    
	}              
    }

    sprintf (cmdBuf, 
	     "/usr/bin/srun --overcommit -n %d hostname > %s", 
	     numProcs,
             hLFileName );    
    system (cmdBuf);

    if ( access (hLFileName, R_OK) == -1 ) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
	    "Can't access %s", hLFileName);
    }

    if ( (fptr = fopen (hLFileName, "r")) == NULL) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "Can't open %s", hLFileName);
	exit(1);
    }
    
    aLine = (char *) malloc (sizeof(char) * PATH_MAX);
    hostList = (char **) malloc (sizeof(char *) * COBOFEN_MAXHL);
    ssize_t len=0;
    while ((len = getline (&aLine, &lLength, fptr)) > 0) {
        /* 
         * len will be greater than 1: should never be 0. 
         * Any 0 condition will return -1 instead. 
         */
	aLine[len-1] = '\0';
	hostList[h] = strdup (aLine);
	h++;
    }

    if (h < 1) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "Empty hosts list");
	exit(1);	
    }

    portList = (int *) malloc (sizeof(int)*COBOFEN_PORT_RANGE);
    for (p=0; p < COBOFEN_PORT_RANGE; ++p) {
	portList[p] = COBOFEN_BASE_PORT+p;
    }

    /*****************************************************************
     * Fork/Exec back-ends 
     *
     *****************************************************************/

     if ( fork () == 0 ) {
         /* CHILD */
         sleep (5);
         char argbuf[128];
         char *newargv[5];
         newargv[0] = strdup ("/usr/bin/srun");
         newargv[1] = strdup ("--overcommit");
         snprintf (argbuf, 128, "-n%d", numProcs);
         newargv[2] = strdup (argbuf);
         newargv[3] = strdup ("./mp_vanilla_cobo_be");
         newargv[4] = NULL;
         execv ((const char *) newargv[0], (char * const *) newargv);
         /* SINK */
     }

    rc = cobo_server_open (COBOFEN_SESSION, hostList, h,portList, p);

    sleep(60);

    if (rc != COBO_SUCCESS) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "cobo_server_open returned failure");
	exit(1);
    }

    mp_cobo_say_msg (COBOOUT_PREFIX, 0,
        "After cobo_server_open");

    rc = cobo_server_close();
    if (rc != COBO_SUCCESS) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "cobo_server_close returned failure");
	exit(1);
    }

    return EXIT_SUCCESS;
}


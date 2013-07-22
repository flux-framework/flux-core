/* mp_cobo_be.c - exercise public interfaces */

#include "mp_util.h"
#include "cobo_be.h"


int 
main (int argc, char *argv[])
{
    int rc            = 0;
    int rank          = -1;
    int size          = -1;
    int psfd          = -1;
    int bcBuf         = -1;
    unsigned int sid  = 0;
    char *ip          = NULL;
    char *port        = NULL;
    double start_ts, end_ts;
    double smallest, largest;
    double *start_ts_array = NULL;
    double *end_ts_array = NULL;
    ip = strtok (argv[1], ":"); 
    port = strtok (NULL, ":");

    if ((ip == NULL) || (port == NULL)) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1, "Is fenIp:port ill-formated?");
	exit(1);
    }


    /*****************************************************************
     *                    COBO Begins
     *
     *****************************************************************/
    start_ts  = gettimeofdayD ();

    rc = cobo_open (&argc, &argv,
                    (const char *) ip,
                    atoi(port),
                    &size,
                    &rank,
                    &sid);
    if  (rc != COBO_SUCCESS) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1, 
            "cobo_open failed.", 1);
	exit(1);
    }

    end_ts = gettimeofdayD ();

    if (argc != 2) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1, "Usage: BackEnd fenIp:port");
	exit(1);
    }

   if (rank == 0) {
        start_ts_array = (double *) malloc (size * sizeof (double));
        end_ts_array = (double *) malloc (size * sizeof (double));
    }

    if (cobo_gather (&start_ts, sizeof(start_ts),
                     start_ts_array, 0) != COBO_SUCCESS ) {
        mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "cobo_gather returned failure", 1);
        exit(1);

    }

    if (cobo_gather (&end_ts, sizeof(end_ts),
                     end_ts_array, 0) != COBO_SUCCESS ) {
        mp_cobo_say_msg (COBOOUT_PREFIX, 1,
            "cobo_gather returned failure", 1);
        exit(1);

    }

    if ( rank == 0 ) {
        int i;
        smallest = start_ts_array[0];
        largest = end_ts_array[0];
        for ( i = 0; i < size; ++i ) {
            /* mp_cobo_say_msg (COBOOUT_PREFIX, 0,
             *   "%f: %f", start_ts_array[i], 
             *   end_ts_array[i]);
             */
            if (start_ts_array[i] < smallest) {
                smallest = start_ts_array[i];
            } 
            if (end_ts_array[i] > largest) {
                largest = end_ts_array[i];
            }
        }

        double elapsed_time = largest - smallest; 
        mp_cobo_say_msg (COBOOUT_PREFIX, 0,
            "Elapsed time of COBO's port range-based up-down connection"
            " method at %d: %f seconds",
            size, elapsed_time);
        if (elapsed_time < 0.0) {
            mp_cobo_say_msg (COBOOUT_PREFIX, 0,
                "Severe clock skew! can't trust the elapsed time reported");
        }
    }

    if (rank == 0)
        mp_cobo_say_msg (COBOOUT_PREFIX, 0, "After cobo_open.");

    if (rank == 0) {
        cobo_get_parent_socket(&psfd);
        if (read (psfd, &bcBuf, sizeof(bcBuf)) != sizeof(bcBuf)) {
	    mp_cobo_say_msg (COBOOUT_PREFIX, 1, "read failed.");
	    exit(1);
        } 
    }
    
    rc = cobo_bcast (&bcBuf, sizeof(bcBuf), 0);
    if ( rc != COBO_SUCCESS ) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1, "cobo_bcast failed.");
	exit(1);
    }

    /* mp_cobo_say_msg (COBOOUT_PREFIX, 0, "Rank[%d]: %d ", rank, bcBuf);
    */
    rc = cobo_barrier ();
    if ( rc != COBO_SUCCESS ) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1, "cobo_barrier failed.");
	exit(1);
    }

    if ( bcBuf != (int) sid ) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1, "Info equality doesn't hold: failure.");
	mp_cobo_say_msg (COBOOUT_PREFIX, 1, "Proceed never the less.");
    }

    if ( rank == 0 ) {
        if (write (psfd, &bcBuf, sizeof(bcBuf)) != sizeof(bcBuf)) {
	    mp_cobo_say_msg (COBOOUT_PREFIX, 1, "write failed.");
	    exit(1);
        } 
    }

    rc = cobo_close ();    
    if  (rc != COBO_SUCCESS) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1, "cobo_close failed.");
	exit(1);
    }

    if ( rank == 0 ) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 0, "Backends ran to completion.");
	mp_cobo_say_msg (COBOOUT_PREFIX, 0, "Should you have no failures, test succeeded.");
    }

    /*****************************************************************
     *                    COBO Ends
     *
     *****************************************************************/
    
    return EXIT_SUCCESS;
}

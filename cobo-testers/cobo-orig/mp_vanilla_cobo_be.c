/* mp_cobo_be.c - exercise public interfaces */

#include "mp_util.h"
#include "cobo.h"

int 
main (int argc, char *argv[])
{
    int p             = 0;
    int rc            = 0;
    int rank          = -1;
    int size          = -1;
    int *portList     = NULL;
    double *start_ts_array = NULL; 
    double *end_ts_array = NULL; 
    double start_ts, end_ts, smallest, largest;

    if (argc != 1) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1, "Usage: BackEnd");
	exit(1);
    }

    portList = (int *) malloc (sizeof(int)*COBOFEN_PORT_RANGE);
    for (p=0; p < COBOFEN_PORT_RANGE; ++p) {
	portList[p] = COBOFEN_BASE_PORT+p;
    }

    start_ts = gettimeofdayD ();

    rc = cobo_open (COBOFEN_SESSION, portList, p, &rank, &size);
    if  (rc != COBO_SUCCESS) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1, 
            "cobo_server_open returned failure", 1);
	exit(1);
    }

    end_ts = gettimeofdayD ();
    
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
        int i=0;
        double largest_start, smallest_end;
        largest_start = smallest = start_ts_array[0];
        smallest_end = largest = end_ts_array[0];
        for ( i = 0; i < size; ++i ) {
            if (start_ts_array[i] < smallest) {
                smallest = start_ts_array[i]; 
            } 
            if (start_ts_array[i]  > largest_start) {
                largest_start = start_ts_array[i];
            }
            if (end_ts_array[i] > largest) {
                largest = end_ts_array[i];
            }
            if (end_ts_array[i] < smallest_end) {
                smallest_end = end_ts_array[i];
            }
        }

        double elapsed_time = largest - smallest;
        mp_cobo_say_msg (COBOOUT_PREFIX, 0,
            "Elapsed time of COBO's port range-based up-down connection"
            " method at %d is %f seconds", 
            size, elapsed_time);
        mp_cobo_say_msg (COBOOUT_PREFIX, 0,
            "Start min (%f), Start max (%f)", smallest, largest_start);
        mp_cobo_say_msg (COBOOUT_PREFIX, 0,
            "End min (%f), End max (%f)", smallest_end, largest);
        if (elapsed_time < 0.0) {
            mp_cobo_say_msg (COBOOUT_PREFIX, 0,
                "Severe clock skew! can't trust the elapsed time reported");
        }
    }

    int bcBuf = 0;
    if ( rank == 0) 
        bcBuf = COBOFEN_SESSION;

    rc = cobo_bcast (&bcBuf, sizeof(bcBuf), 0);
    if ( rc != COBO_SUCCESS ) {
        mp_cobo_say_msg (COBOOUT_PREFIX, 1, "cobo_bcast failed.");
        exit(1);
    }
    rc = cobo_barrier ();
    if ( rc != COBO_SUCCESS ) {
        mp_cobo_say_msg (COBOOUT_PREFIX, 1, "cobo_barrier failed.");
        exit(1);
    }

    if ( bcBuf != (int) COBOFEN_SESSION ) {
        mp_cobo_say_msg (COBOOUT_PREFIX, 1, "Info equality doesn't hold: failure.");
        mp_cobo_say_msg (COBOOUT_PREFIX, 1, "Proceed never the less.");
    }

    if (rank == 0)
        mp_cobo_say_msg (COBOOUT_PREFIX, 0, "After cobo_open");


    /*****************************************************************
     *                    Break Point Ends
     *
     *****************************************************************/
    
    rc = cobo_close ();    
    if  (rc != COBO_SUCCESS) {
	mp_cobo_say_msg (COBOOUT_PREFIX, 1, "cobo_server_open returned failure");
	exit(1);
    }
    
    return EXIT_SUCCESS;
}

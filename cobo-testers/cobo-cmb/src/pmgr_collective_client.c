/*
 * Copyright (c) 2009, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Adam Moody <moody20@llnl.gov>.
 * LLNL-CODE-411040.
 * All rights reserved.
 * This file is part of the PMGR_COLLECTIVE library.
 * For details, see https://sourceforge.net/projects/pmgrcollective.
 * Please also read this file: LICENSE.TXT.
*/

/*
 * PMGR_COLLECTIVE ============================================================
 * This protocol enables MPI to bootstrap itself through a series of collective
 * operations.  The collective operations are modeled after MPI collectives --
 * all tasks must call them in the same order and with consistent parameters.
 *
 * MPI may invoke any number of collectives, in any order, passing an arbitrary
 * amount of data.  All message sizes are specified in bytes.
 * PMGR_COLLECTIVE ============================================================
 *
 * This file implements the interface used by the MPI tasks (clients).
 *
 * An MPI task should make calls in the following sequenece:
 *
 *   pmgr_init
 *   pmgr_open
 *   [collectives]
 *   pmgr_close
 *   pmgr_finalize
 *
 * MPI may invoke any number of collectives, in any order, passing an arbitrary
 * amount of data.  All message sizes are specified in bytes.
 *
 * All functions return PMGR_SUCCESS on successful completion.
*/

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include "pmgr_collective_client.h"
#include "pmgr_collective_client_common.h"
#include "pmgr_collective_client_mpirun.h"
#include "pmgr_collective_client_tree.h"
#include "pmgr_collective_client_slurm.h"

#ifdef HAVE_PMI
#include "pmi.h"
#endif

#ifdef HAVE_FLUX_CMB
typedef int bool;
#include <json/json.h>
#include "cmb.h"
#endif

#define PMGR_DEBUG_LEVELS (3)

/* total time to wait to get through pmgr_open */
#ifndef MPIRUN_OPEN_TIMEOUT
#define MPIRUN_OPEN_TIMEOUT (30*60) /* seconds, total time to wait to get through pmgr_open() */
#endif

/* set env variable to configure socket timeout parameters */
#ifndef MPIRUN_CONNECT_TRIES
#define MPIRUN_CONNECT_TRIES (10)   /* number of times to attempt to connect to IP:port before giving up */
#endif
#ifndef MPIRUN_CONNECT_TIMEOUT
#define MPIRUN_CONNECT_TIMEOUT (60) /* seconds, we only apply this when we know the IP:port is correct, so it can be high */
#endif
#ifndef MPIRUN_CONNECT_BACKOFF
#define MPIRUN_CONNECT_BACKOFF (5) /* seconds, max amount of time to sleep before trying to connect again */
#endif
#ifndef MPIRUN_CONNECT_RANDOM
#define MPIRUN_CONNECT_RANDOM (1) /* enable/disable randomized option for backoff */
#endif

#ifndef MPIRUN_CONNECT_DOWN
#define MPIRUN_CONNECT_DOWN (0) /* whether to connect tree from parent to children (down) or children to parent (up) */
#endif

#ifndef MPIRUN_PORT_SCAN_TIMEOUT         /* total time we'll try to connect to a host before throwing a fatal error */
#define MPIRUN_PORT_SCAN_TIMEOUT (30*60) /* seconds */
#endif
#ifndef MPIRUN_PORT_SCAN_CONNECT_TIMEOUT         /* time to wait before giving up on connect call */
#define MPIRUN_PORT_SCAN_CONNECT_TIMEOUT (10000) /* millisecs */
#endif
#ifndef MPIRUN_PORT_SCAN_CONNECT_ATTEMPTS     /* number of consecutive times to try a given port */
#define MPIRUN_PORT_SCAN_CONNECT_ATTEMPTS (1) /* seconds */
#endif
#ifndef MPIRUN_PORT_SCAN_CONNECT_SLEEP      /* time to sleep between consecutive connect calls */
#define MPIRUN_PORT_SCAN_CONNECT_SLEEP (10) /* millisecs */
#endif
#ifndef MPIRUN_AUTHENTICATE_ENABLE
#define MPIRUN_AUTHENTICATE_ENABLE (1)
#endif
#ifndef MPIRUN_AUTHENTICATE_TIMEOUT         /* time to wait for read to complete after making connection */
#define MPIRUN_AUTHENTICATE_TIMEOUT (60000) /* milliseconds */
#endif

/* set env variable whether to use trees */
#ifndef MPIRUN_USE_TREES
#define MPIRUN_USE_TREES (1)
#endif

/* whether to invoke PMI library to bootstrap PMGR_COLLECTIVE */
#ifndef MPIRUN_PMI_ENABLE
#define MPIRUN_PMI_ENABLE (0)
#endif

/* whether to invoke FLUX's CMB to bootstrap PMGR_COLLECTIVE */
#ifndef MPIRUN_FLUX_CMB_ENABLE
#define MPIRUN_FLUX_CMB_ENABLE (0)
#endif

/* whether to use shared memory so that only one proc per node must do network communication */
#ifndef MPIRUN_SHM_ENABLE
#define MPIRUN_SHM_ENABLE (1)
#endif
#ifndef MPIRUN_SHM_THRESHOLD
#define MPIRUN_SHM_THRESHOLD (1024)
#endif

/* total time to get through pmgr_open */
int mpirun_open_timeout = MPIRUN_OPEN_TIMEOUT;

/* startup time, time between starting pmgr_open and finishing pmgr_close */
struct timeval time_open;
struct timeval time_close;

/* parameters for connection attempts */
int mpirun_connect_tries    = MPIRUN_CONNECT_TRIES;
int mpirun_connect_timeout  = MPIRUN_CONNECT_TIMEOUT;
int mpirun_connect_backoff  = MPIRUN_CONNECT_BACKOFF;
int mpirun_connect_random   = MPIRUN_CONNECT_RANDOM;

int mpirun_connect_down = MPIRUN_CONNECT_DOWN;

unsigned pmgr_backoff_rand_seed;

/* time to wait for replies while authenticating connections */
int mpirun_authenticate_enable  = MPIRUN_AUTHENTICATE_ENABLE;
int mpirun_authenticate_timeout = MPIRUN_AUTHENTICATE_TIMEOUT;

/* parameters for connection attempts when conduction a port scan */
int mpirun_port_scan_timeout              = MPIRUN_PORT_SCAN_TIMEOUT;
int mpirun_port_scan_connect_timeout      = MPIRUN_PORT_SCAN_CONNECT_TIMEOUT;
int mpirun_port_scan_connect_attempts     = MPIRUN_PORT_SCAN_CONNECT_ATTEMPTS;
int mpirun_port_scan_connect_sleep        = MPIRUN_PORT_SCAN_CONNECT_SLEEP;

/* set envvar MPIRUN_USE_TREES={0,1} to disable/enable tree algorithms */
static int mpirun_use_trees = MPIRUN_USE_TREES;

/* whether to use PMI library to bootstrap */
int mpirun_pmi_enable = MPIRUN_PMI_ENABLE;
int mpirun_flux_cmb_enable = MPIRUN_FLUX_CMB_ENABLE;
x_comm_fab_cxt comm_fab_cxt = {0};
int mpirun_shm_enable = MPIRUN_SHM_ENABLE;
int mpirun_shm_threshold = MPIRUN_SHM_THRESHOLD;

static int   pmgr_nprocs = -1;
static int   pmgr_id     = -1;

/* track whether we are in between pmgr_open and pmgr_close */
static int   pmgr_is_open = 0;

/* records details on server process to bootstrap */
char* mpirun_hostname = NULL;
int   mpirun_port = 0;

/* binomial tree containing all procs in job */
pmgr_tree_t pmgr_tree_all;

/* 
 * =============================
 * Functions to open/close/gather/bcast the TCP/socket tree.
 * =============================
 */

int pmgr_get_parent_socket (int *fd)
{
    *fd = pmgr_tree_all.parent_fd;

    return PMGR_SUCCESS;
}

/* abort all open trees */
int pmgr_abort_trees()
{
    pmgr_tree_abort(&pmgr_tree_all);

    /* send CLOSE op code to mpirun, then close socket */
    if (pmgr_mpirun_is_open()) {
        pmgr_mpirun_close();
    }

    return PMGR_SUCCESS;
}

/*
 * =============================
 * The pmgr_* collectives are the user interface (what the MPI tasks call).
 * =============================
 */

/* Perform barrier, each task writes an int then waits for an int */
int pmgr_barrier()
{
    struct timeval start, end;
    pmgr_gettimeofday(&start);
    pmgr_debug(3, "Starting pmgr_barrier()");

    int rc = PMGR_SUCCESS;

    /* bail out if we're not open */
    if (!pmgr_is_open) {
        pmgr_error("Must call pmgr_open() before pmgr_barrier() @ file %s:%d",
            __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* check whether we have an mpirun process */
    if (pmgr_nprocs > 1) {
        if (pmgr_tree_is_open(&pmgr_tree_all)) {
            /* just issue a check tree using success */
            rc = pmgr_tree_check(&pmgr_tree_all, 1);
        } else if (pmgr_mpirun_is_open()) {
            /* trees aren't enabled, use mpirun to do barrier */
            rc = pmgr_mpirun_barrier();
        } else {
            pmgr_error("No method to communication with other procs @ file %s:%d",
                __FILE__, __LINE__
            );
            return PMGR_FAILURE;
        }
    }
    /* if there is no mpirun_process, this is just a barrier over a single client process */

    pmgr_gettimeofday(&end);
    pmgr_debug(2, "Exiting pmgr_barrier(), took %f seconds for %d procs", pmgr_getsecs(&end,&start), pmgr_nprocs);
    return rc;
}

/*
 * Perform MPI-like Allreduce of a single int64_t from each task
 */
int pmgr_allreduce_int64t(int64_t* sendint, int64_t* recvint, pmgr_op op)
{
    struct timeval start, end;
    pmgr_gettimeofday(&start);
    pmgr_debug(3, "Starting pmgr_allreduce_int64t()");

    int rc = PMGR_SUCCESS;

    /* bail out if we're not open */
    if (!pmgr_is_open) {
        pmgr_error("Must call pmgr_open() before pmgr_allreducemaxint() @ file %s:%d",
            __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* verify that user provided some data */
    if (sendint == NULL || recvint == NULL) {
        return PMGR_FAILURE;
    }

    /* reduce the maximum integer to rank 0 and store in max */
    if (pmgr_tree_is_open(&pmgr_tree_all)) {
        /* use the tree to do our reduction */
        rc = pmgr_tree_allreduce_int64t(&pmgr_tree_all, sendint, recvint, op);
        if (rc != PMGR_SUCCESS) {
            return rc;
        }
    } else {
        /* have no tree, gather all values to rank 0 */
        /* allocate space to receive ints from everyone */
        int64_t* all = NULL;
        if (pmgr_me == 0) {
            all = (int64_t*) pmgr_malloc((size_t)pmgr_nprocs * sizeof(int64_t), "One int64_t for each task");
        }

        /* gather all ints to rank 0 */
        rc = pmgr_gather((void*) sendint, sizeof(int64_t), (void*) all, 0);
        if (rc != PMGR_SUCCESS) {
            if (pmgr_me == 0) {
                pmgr_free(all);
            }
            return rc;
        }

        /* rank 0 searches through list for maximum value */
        if (pmgr_me == 0) {
            int i;
            *recvint = all[0];
            for (i = 1; i < pmgr_nprocs; i++) {
                if (op == PMGR_SUM) {
                    *recvint += all[i];
                } else if (op == PMGR_MAX && all[i] > *recvint) {
                    *recvint = all[i];
                }
            }
            pmgr_free(all);
        }

        /* broadcast max int from rank 0 and set recvint */
        rc = pmgr_bcast((void*) recvint, sizeof(int64_t), 0);
    }

    pmgr_gettimeofday(&end);
    pmgr_debug(2, "Exiting pmgr_allreduce_int64t(), took %f seconds for %d procs", pmgr_getsecs(&end,&start), pmgr_nprocs);
    return rc;
}

/*
 * Perform MPI-like Broadcast, root writes sendcount bytes from buf,
 * into mpirun_socket, all receive sendcount bytes into buf
 */
int pmgr_bcast(void* buf, int sendcount, int root)
{
    struct timeval start, end;
    pmgr_gettimeofday(&start);
    pmgr_debug(3, "Starting pmgr_bcast()");

    int rc = PMGR_SUCCESS;

    /* bail out if we're not open */
    if (!pmgr_is_open) {
        pmgr_error("Must call pmgr_open() before pmgr_bcast() @ file %s:%d",
            __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* verify that user provided some data */
    if (buf == NULL || sendcount <= 0) {
        return PMGR_FAILURE;
    }

    /* check whether we have an mpirun process */
    if (pmgr_nprocs > 1) {
        /* if root is rank 0 and bcast tree is enabled, use it */
        /* (this is a common case) */
        if (root == 0 && pmgr_tree_is_open(&pmgr_tree_all)) {
            rc = pmgr_tree_bcast(&pmgr_tree_all, buf, sendcount);
        } else if (pmgr_mpirun_is_open()) {
            rc = pmgr_mpirun_bcast(buf, sendcount, root);
        } else {
            pmgr_error("No method to communication with other procs @ file %s:%d",
                __FILE__, __LINE__
            );
            return PMGR_FAILURE;
        }
    }
    /* if there is no mpirun process, the root is the only process, so there's nothing to do */

    pmgr_gettimeofday(&end);
    pmgr_debug(2, "Exiting pmgr_bcast(), took %f seconds for %d procs", pmgr_getsecs(&end,&start), pmgr_nprocs);
    return rc;
}

/*
 * Perform MPI-like Gather, each task writes sendcount bytes from sendbuf
 * into mpirun_socket, then root receives N*sendcount bytes into recvbuf
 */
int pmgr_gather(void* sendbuf, int sendcount, void* recvbuf, int root)
{
    struct timeval start, end;
    pmgr_gettimeofday(&start);
    pmgr_debug(3, "Starting pmgr_gather()");

    int rc = PMGR_SUCCESS;

    /* bail out if we're not open */
    if (!pmgr_is_open) {
        pmgr_error("Must call pmgr_open() before pmgr_gather() @ file %s:%d",
            __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* verify that user provided some data,
     * note that we don't check recvbuf since only the root will receive anything */
    if (sendbuf == NULL || sendcount <= 0) {
        return PMGR_FAILURE;
    }

    /* check whether we have an mpirun process */
    if (pmgr_nprocs > 1) {
        /* if root is rank 0 and gather tree is enabled, use it */
        /* (this is a common case) */
        if (root == 0 && pmgr_tree_is_open(&pmgr_tree_all)) {
            rc = pmgr_tree_gather(&pmgr_tree_all, sendbuf, sendcount, recvbuf);
        } else if (pmgr_mpirun_is_open()) {
            rc = pmgr_mpirun_gather(sendbuf, sendcount, recvbuf, root);
        } else {
            pmgr_error("No method to communication with other procs @ file %s:%d",
                __FILE__, __LINE__
            );
            return PMGR_FAILURE;
        }
    } else {
        /* just a single process, just copy the data over */
        memcpy(recvbuf, sendbuf, sendcount);
    }

    pmgr_gettimeofday(&end);
    pmgr_debug(2, "Exiting pmgr_gather(), took %f seconds for %d procs", pmgr_getsecs(&end,&start), pmgr_nprocs);
    return rc;
}

/*
 * Perform MPI-like Scatter, root writes N*sendcount bytes from sendbuf
 * into mpirun_socket, then each task receives sendcount bytes into recvbuf
 */
int pmgr_scatter(void* sendbuf, int sendcount, void* recvbuf, int root)
{
    struct timeval start, end;
    pmgr_gettimeofday(&start);
    pmgr_debug(3, "Starting pmgr_scatter()");

    int rc = PMGR_SUCCESS;

    /* bail out if we're not open */
    if (!pmgr_is_open) {
        pmgr_error("Must call pmgr_open() before pmgr_scatter() @ file %s:%d",
            __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* verify that user provided some data,
     * note that we don't check sendbuf since only the root will send anything */
    if (recvbuf == NULL || sendcount <= 0) {
        return PMGR_FAILURE;
    }

    /* check whether we have an mpirun process */
    if (pmgr_nprocs > 1) {
        /* if root is rank 0 and gather tree is enabled, use it */
        /* (this is a common case) */
        if (root == 0 && pmgr_tree_is_open(&pmgr_tree_all)) {
            rc = pmgr_tree_scatter(&pmgr_tree_all, sendbuf, sendcount, recvbuf);
        } else if (pmgr_mpirun_is_open()) {
            rc = pmgr_mpirun_scatter(sendbuf, sendcount, recvbuf, root);
        } else {
            pmgr_error("No method to communication with other procs @ file %s:%d",
                __FILE__, __LINE__
            );
            return PMGR_FAILURE;
        }
    } else {
        /* just a single process, just copy the data over */
        memcpy(recvbuf, sendbuf, sendcount);
    }

    pmgr_gettimeofday(&end);
    pmgr_debug(2, "Exiting pmgr_scatter(), took %f seconds for %d procs", pmgr_getsecs(&end,&start), pmgr_nprocs);
    return rc;
}

/*
 * Perform MPI-like Allgather, each task writes sendcount bytes from sendbuf
 * into mpirun_socket, then receives N*sendcount bytes into recvbuf
 */
int pmgr_allgather(void* sendbuf, int sendcount, void* recvbuf)
{
    struct timeval start, end;
    pmgr_gettimeofday(&start);
    pmgr_debug(3, "Starting pmgr_allgather()");

    int rc = PMGR_SUCCESS;

    /* bail out if we're not open */
    if (!pmgr_is_open) {
        pmgr_error("Must call pmgr_open() before pmgr_allgather() @ file %s:%d",
            __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* verify that user provided some data */
    if (sendbuf == NULL || recvbuf == NULL || sendcount <= 0) {
        return PMGR_FAILURE;
    }

    /* check whether we have an mpirun process */
    if (pmgr_nprocs > 1) {
        if (pmgr_tree_is_open(&pmgr_tree_all)) {
            /* gather data to rank 0 */
            int tmp_rc;
            tmp_rc = pmgr_tree_gather(&pmgr_tree_all, sendbuf, sendcount, recvbuf);
            if (tmp_rc != PMGR_SUCCESS) {
              rc = tmp_rc;
            }

            /* broadcast data from rank 0 */
            tmp_rc = pmgr_tree_bcast(&pmgr_tree_all, recvbuf, sendcount * pmgr_nprocs);
            if (tmp_rc != PMGR_SUCCESS) {
              rc = tmp_rc;
            }
        } else if (pmgr_mpirun_is_open()) {
            /* trees aren't enabled, use mpirun to do allgather */
            rc = pmgr_mpirun_allgather(sendbuf, sendcount, recvbuf);
        } else {
            pmgr_error("No method to communication with other procs @ file %s:%d",
                __FILE__, __LINE__
            );
            return PMGR_FAILURE;
        }
    } else {
        /* just a single process, just copy the data over */
        memcpy(recvbuf, sendbuf, sendcount);
    }

    pmgr_gettimeofday(&end);
    pmgr_debug(2, "Exiting pmgr_allgather(), took %f seconds for %d procs", pmgr_getsecs(&end,&start), pmgr_nprocs);
    return rc;
}

/*
 * Perform MPI-like Alltoall, each task writes N*sendcount bytes from sendbuf
 * into mpirun_socket, then recieves N*sendcount bytes into recvbuf
 */
int pmgr_alltoall(void* sendbuf, int sendcount, void* recvbuf)
{
    struct timeval start, end;
    pmgr_gettimeofday(&start);
    pmgr_debug(3, "Starting pmgr_alltoall()");

    int rc = PMGR_SUCCESS;

    /* bail out if we're not open */
    if (!pmgr_is_open) {
        pmgr_error("Must call pmgr_open() before pmgr_alltoall() @ file %s:%d",
            __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* verify that user provided some data */
    if (sendbuf == NULL || recvbuf == NULL || sendcount <= 0) {
        return PMGR_FAILURE;
    }

    /* check whether we have an mpirun process */
    if (pmgr_nprocs > 1) {
        if (pmgr_tree_is_open(&pmgr_tree_all)) {
            rc = pmgr_tree_alltoall(&pmgr_tree_all, sendbuf, sendcount, recvbuf);
        } else if (pmgr_mpirun_is_open()) {
            rc = pmgr_mpirun_alltoall(sendbuf, sendcount, recvbuf);
        } else {
            pmgr_error("No method to communication with other procs @ file %s:%d",
                __FILE__, __LINE__
            );
            return PMGR_FAILURE;
        }
    } else {
        /* just a single process, just copy the data over */
        memcpy(recvbuf, sendbuf, sendcount);
    }

    pmgr_gettimeofday(&end);
    pmgr_debug(2, "Exiting pmgr_alltoall(), took %f seconds for %d procs", pmgr_getsecs(&end,&start), pmgr_nprocs);
    return rc;
}

/*
 * Perform MPI-like Allgather of NULL-terminated strings (whose lengths may vary
 * from task to task).
 *
 * Each task provides a pointer to its NULL-terminated string as input.
 * Each task then receives an array of pointers to strings indexed by rank number
 * and also a pointer to the buffer holding the string data.
 * When done with the strings, both the array of string pointers and the
 * buffer should be freed.
 *
 * Example Usage:
 *   char host[256], **hosts, *buf;
 *   gethostname(host, sizeof(host));
 *   pmgr_allgatherstr(host, &hosts, &buf);
 *   for(int i=0; i<nprocs; i++) { printf("rank %d runs on host %s\n", i, hosts[i]); }
 *   free(hosts);
 *   free(buf);
 */
int pmgr_allgatherstr(char* sendstr, char*** recvstr, char** recvbuf)
{
    struct timeval start, end;
    pmgr_gettimeofday(&start);
    pmgr_debug(3, "Starting pmgr_allgatherstr()");

    int rc = PMGR_SUCCESS;

    /* bail out if we're not open */
    if (!pmgr_is_open) {
        pmgr_error("Must call pmgr_open() before pmgr_allgatherstr() @ file %s:%d",
            __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* determine max length of send strings */
    int64_t mylen  = strlen(sendstr) + 1;
    int64_t maxlen = 0;
    rc = pmgr_allreduce_int64t(&mylen, &maxlen, PMGR_MAX);
    if (rc != PMGR_SUCCESS) {
        /* if the reduce failed, we can't trust the value of maxlen */
        *recvstr = NULL;
        *recvbuf = NULL;
        return rc;
    }

    /* pad my string to match max length */
    char* mystr = (char*) pmgr_malloc(maxlen, "Padded String");
    memset(mystr, '\0', maxlen);
    strcpy(mystr, sendstr);

    /* allocate enough buffer space to receive a maxlen string from all tasks */
    char* stringbuf = (char*) pmgr_malloc(pmgr_nprocs * maxlen, "String Buffer");

    /* gather strings from everyone */
    rc = pmgr_allgather((void*) mystr, maxlen, (void*) stringbuf);

    /* set up array and free temporary maxlen string */
    char** strings = (char **) pmgr_malloc(pmgr_nprocs * sizeof(char*), "Array of String Pointers");
    int i;
    for (i=0; i<pmgr_nprocs; i++) {
        strings[i] = stringbuf + i*maxlen;
    }
    pmgr_free(mystr);

    *recvstr = strings;
    *recvbuf = stringbuf;

    pmgr_gettimeofday(&end);
    pmgr_debug(2, "Exiting pmgr_allgatherstr(), took %f seconds for %d procs", pmgr_getsecs(&end,&start), pmgr_nprocs);
    return rc;
}

static int pmgr_treeless_aggregate(const void* sendbuf, int64_t sendcount,
    void* recvbuf, int64_t recvcount, int64_t* writte)
{
        /* determine sum of sendcounts from all ranks */

        /* ensure this total fits within the receive buffer */

        /* find max sendcount on all ranks */

        /* allocate enough to hold this amount from each rank,
         * plus an leading int64_t */

        /* gather to rank 0 */

        /* rank 0 copies data into recvbuf */

        /* free temp buffer */

        /* bcast receive buf to all tasks */

    return PMGR_SUCCESS;
}

/* collects data sent by each rank and writes at most max_recvcount bytes
 * into recvbuf.  sendcount may be different on each process, actual number
 * of bytes received provided as output in written.  data is *not* ordered
 * by rank, nor is it guaranteed to be received in the same order on each rank */
int pmgr_aggregate(const void* sendbuf, int64_t sendcount,
    void* recvbuf, int64_t recvcount, int64_t* written)
{
    struct timeval start, end;
    pmgr_gettimeofday(&start);
    pmgr_debug(3, "Starting pmgr_allgatherstr()");

    int rc = PMGR_SUCCESS;

    /* bail out if we're not open */
    if (!pmgr_is_open) {
        pmgr_error("Must call pmgr_open() before pmgr_aggregate() @ file %s:%d",
            __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* verify that user provided some data */
    if (sendbuf == NULL || recvbuf == NULL ||
        sendcount < 0 || recvcount < 0 || written == NULL)
    {
        return PMGR_FAILURE;
    }

    /* check whether we have an mpirun process */
    if (pmgr_nprocs > 1) {
        if (pmgr_tree_is_open(&pmgr_tree_all)) {
            rc = pmgr_tree_aggregate(&pmgr_tree_all, sendbuf, sendcount,
                recvbuf, recvcount, written
            );
        } else {
            rc = pmgr_treeless_aggregate(sendbuf, sendcount,
                recvbuf, recvcount, written
            );
        }
    } else {
        /* just a single process, just copy the data over */
        if (recvcount < sendcount) {
            return PMGR_FAILURE;
        }
        if (sendcount > 0) {
            memcpy(recvbuf, sendbuf, sendcount);
        }
        *written = sendcount;
    }

    pmgr_gettimeofday(&end);
    pmgr_debug(2, "Exiting pmgr_aggregate(), took %f seconds for %d procs", pmgr_getsecs(&end,&start), pmgr_nprocs);
    return rc;
}

int pmgr_open()
{
    struct timeval start, end;
    pmgr_gettimeofday(&time_open);
    pmgr_gettimeofday(&start);
    pmgr_debug(3, "Starting pmgr_open()");

    /* seed used for random backoff in pmgr_connect later */
    pmgr_backoff_rand_seed = time_open.tv_usec + pmgr_me;

    /* check whether we have an mpirun process */
    if (pmgr_nprocs > 1) {
        if (!mpirun_pmi_enable && !(mpirun_shm_enable && pmgr_nprocs >= mpirun_shm_threshold)) {
            /* open connection back to mpirun process */
            if (pmgr_mpirun_open(pmgr_nprocs, pmgr_me) != PMGR_SUCCESS) {
                exit(1);
            }
        }

        /* open up socket tree, if enabled */
        if (mpirun_use_trees) {
            /* set up our authentication text to verify connections */
            char auth_text[1024];
            size_t auth_len = snprintf(auth_text, sizeof(auth_text), "%d::%s", pmgr_id, "ALL") + 1;
            if (auth_len > sizeof(auth_text)) {
                pmgr_error("Authentication text too long, %d bytes exceeds limit %d @ file %s:%d",
                    auth_len, sizeof(auth_text), __FILE__, __LINE__
                );
                exit(1);
            }

            /* now open our tree */
            if (pmgr_tree_open(&pmgr_tree_all, pmgr_nprocs, pmgr_me, auth_text) != PMGR_SUCCESS) {
              exit(1);
            }

            /* close off our N-to-1 connections to srun if we opened them */
            if (pmgr_mpirun_is_open()) {
                pmgr_mpirun_close();
            }
        }
    }
    /* just a single process, we don't need to open a connection here */

    /* mark our state as opened */
    pmgr_is_open = 1;

    pmgr_gettimeofday(&end);
    pmgr_debug(2, "Exiting pmgr_open(), took %f seconds for %d procs", pmgr_getsecs(&end,&start), pmgr_nprocs);
    return PMGR_SUCCESS;
}

/*
 * Closes the mpirun socket
 */
int pmgr_close()
{
    struct timeval start, end;
    pmgr_gettimeofday(&start);
    pmgr_debug(3, "Starting pmgr_close()");

    /* check whether we have an mpirun process */
    if (pmgr_nprocs > 1) {
        /* shut down the tree, if it's open */
        if (pmgr_tree_is_open(&pmgr_tree_all)) {
            /* issue a barrier before closing tree to check that everyone makes it here */
            pmgr_tree_check(&pmgr_tree_all, 1);
            pmgr_tree_close(&pmgr_tree_all);
        }

        if (pmgr_mpirun_is_open()) {
            pmgr_mpirun_close();
        }
    }
    /* just a single process, there is nothing to close */

    /* switch our state to closed */
    pmgr_is_open = 0;

    pmgr_gettimeofday(&end);
    pmgr_gettimeofday(&time_close);
    pmgr_debug(2, "Exiting pmgr_close(), took %f seconds for %d procs", pmgr_getsecs(&end,&start), pmgr_nprocs);
    pmgr_debug(1, "Total time from pmgr_open() to pmgr_close() took %f seconds for %d procs",
        pmgr_getsecs(&time_close, &time_open), pmgr_nprocs
    );
    return PMGR_SUCCESS;
}

/*
 * =============================
 * Handle init and finalize
 * =============================
 */

int pmgr_init(int *argc_p, char ***argv_p, int *np_p, int *me_p, int *id_p)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    char *value;

    struct timeval start, end;
    pmgr_gettimeofday(&start);

    pmgr_echo_debug = 0;

    pmgr_tree_init_null(&pmgr_tree_all);

    /* =======================================================
     * Until told otherwise, assume we are rank 0 of a 1-task MPI job
     * this enables serial launching, e.g., "./mpiHello" vs "mpirun -np 1 ./mpiHello"
     * TODO: may want to protect this usage via a compile flag and/or env var
     * ======================================================= */

    /* Take a stab at something unique for the id (timestamp.secs | pid)
     * TODO: !!NOTE!!
     * Using a pid in the jobid *ONLY* works for a single process job
     * Obviously, multiple tasks will have different pids */
    pmgr_id = 0x7FFFFFFF & ((start.tv_sec << 16) | (0x0000FFFF & getpid()));

    pmgr_me     = 0;
    pmgr_nprocs = 1;

    mpirun_hostname = NULL;
    mpirun_port = 1;

    /* =======================================================
     * Get information from environment, not from the argument list
     * ======================================================= */

    /* if MPIRUN_RANK is set, require RANK, NPROCS, ID, HOST, and PORT to all be set */
    /* this ensures that if one process aborts in a multitask job,
     * then something is there to abort the others, namely the mpirun process */
    if ((value = pmgr_getenv("MPIRUN_RANK", ENV_OPTIONAL)) != NULL) {
        /* MPI rank of current process */
        pmgr_me = atoi(pmgr_getenv("MPIRUN_RANK", ENV_REQUIRED));

        /* number of MPI processes in job */
        pmgr_nprocs = atoi(pmgr_getenv("MPIRUN_NPROCS", ENV_REQUIRED));

        /* unique jobid of current application */
        pmgr_id = atoi(pmgr_getenv("MPIRUN_ID", ENV_REQUIRED));

        /* mpirun host IP string in dotted decimal notation */
        mpirun_hostname = strdup(pmgr_getenv("MPIRUN_HOST", ENV_REQUIRED));

        /* mpirun port number */
        mpirun_port = atoi(pmgr_getenv("MPIRUN_PORT", ENV_REQUIRED));
    }

    if ((value = pmgr_getenv("MPIRUN_OPEN_TIMEOUT", ENV_OPTIONAL))) {
        mpirun_open_timeout = atoi(value);
    }

    if ((value = pmgr_getenv("MPIRUN_CONNECT_TRIES", ENV_OPTIONAL))) {
        mpirun_connect_tries = atoi(value);
    }

    /* seconds */
    if ((value = pmgr_getenv("MPIRUN_CONNECT_TIMEOUT", ENV_OPTIONAL))) {
        mpirun_connect_timeout = atoi(value);
    }

    /* seconds */
    if ((value = pmgr_getenv("MPIRUN_CONNECT_BACKOFF", ENV_OPTIONAL))) {
        mpirun_connect_backoff = atoi(value);
    }

    /* enable/disable radomized option in backoff */
    if ((value = pmgr_getenv("MPIRUN_CONNECT_RANDOM", ENV_OPTIONAL))) {
        mpirun_connect_random = atoi(value);
    }

    /* whether to connect tree from parent to children (down) or children to parent (up) */
    if ((value = pmgr_getenv("MPIRUN_CONNECT_DOWN", ENV_OPTIONAL))) {
        mpirun_connect_down = atoi(value);
    }

    /* MPIRUN_USE_TREES={0,1} disables/enables tree algorithms */
    if ((value = pmgr_getenv("MPIRUN_USE_TREES", ENV_OPTIONAL))) {
        mpirun_use_trees = atoi(value);
    }

    /* use pmi instead of socket connections to mpirun */
    if ((value = pmgr_getenv("MPIRUN_PMI_ENABLE", ENV_OPTIONAL))) {
#ifdef HAVE_PMI
        mpirun_pmi_enable = atoi(value);
#else /* ifdef HAVE_PMI */
        /* PMI was not compiled in, warn user that we're ignoring this value */
        if (pmgr_me == 0) {
            pmgr_error("Not built with PMI support, ignoring MPIRUN_PMI_ENABLE @ %s:%d",
                __FILE__, __LINE__
            );
        }
#endif /* ifdef HAVE_PMI */
    }

    /* use pmi instead of socket connections to mpirun */
    if ((value = pmgr_getenv("MPIRUN_FLUX_CMB_ENABLE", ENV_OPTIONAL))) {
#ifdef HAVE_FLUX_CMB
        mpirun_flux_cmb_enable = atoi(value);
#else /* ifdef HAVE_PMI */
        /* PMI was not compiled in, warn user that we're ignoring this value */
        if (pmgr_me == 0) {
            pmgr_error("Not built with FLUX CMB support, ignoring MPIRUN_FLUX_CMB_ENABLE @ %s:%d",
                __FILE__, __LINE__
            );
        }
#endif /* ifdef HAVE_FLUX_CMB */
    }

    /* whether to use /dev/shm to start jobs */
    if ((value = pmgr_getenv("MPIRUN_SHM_ENABLE", ENV_OPTIONAL))) {
        mpirun_shm_enable = atoi(value);
    }

    /* minimum number of tasks to switch to /dev/shm */
    if ((value = pmgr_getenv("MPIRUN_SHM_THRESHOLD", ENV_OPTIONAL))) {
        mpirun_shm_threshold = atoi(value);
    }

    /* whether to authenticate connections */
    if ((value = pmgr_getenv("MPIRUN_AUTHENTICATE_ENABLE", ENV_OPTIONAL))) {
        mpirun_authenticate_enable = atoi(value);
    }

    /* time to wait for a reply when authenticating a new connection (miilisecs) */
    if ((value = pmgr_getenv("MPIRUN_AUTHENTICATE_TIMEOUT", ENV_OPTIONAL))) {
        mpirun_authenticate_timeout = atoi(value);
    }

    /* total time to attempt to connect to a host before aborting (seconds) */
    if ((value = pmgr_getenv("MPIRUN_PORT_SCAN_TIMEOUT", ENV_OPTIONAL))) {
        mpirun_port_scan_timeout = atoi(value);
    }

    /* time to wait on connect call before giving up (millisecs) */
    if ((value = pmgr_getenv("MPIRUN_PORT_SCAN_CONNECT_TIMEOUT", ENV_OPTIONAL))) {
        mpirun_port_scan_connect_timeout = atoi(value);
    }

    /* number of times to attempt connect call to given IP:port */
    if ((value = pmgr_getenv("MPIRUN_PORT_SCAN_CONNECT_ATTEMPTS", ENV_OPTIONAL))) {
        mpirun_port_scan_connect_attempts = atoi(value);
    }

    /* time to wait between making consecutive connect attempts to a given IP:port (millisecs) */
    if ((value = pmgr_getenv("MPIRUN_PORT_SCAN_CONNECT_SLEEP", ENV_OPTIONAL))) {
        mpirun_port_scan_connect_sleep = atoi(value);
    }

    /* initialize PMI library if we're using it, and get rank, ranks, and jobid from PMI */
    if (mpirun_pmi_enable) {
#ifdef HAVE_PMI
        /* initialize the PMI library */
        int spawned = 0;
        if (PMI_Init(&spawned) != PMI_SUCCESS) {
            pmgr_error("Failed to initialize PMI library @ file %s:%d",
                __FILE__, __LINE__
            );
            PMI_Abort(1, "Failed to initialize PMI library");
        }
        if (spawned) {
            pmgr_error("Spawned processes not supported @ file %s:%d",
                __FILE__, __LINE__
            );
            PMI_Abort(1, "Spawned processes not supported");
        }

        /* get my rank */
        if (PMI_Get_rank(&pmgr_me) != PMI_SUCCESS) {
            pmgr_error("Getting rank @ file %s:%d",
                __FILE__, __LINE__
            );
            PMI_Abort(1, "Failed to get rank from PMI");
        }

        /* get the number of ranks in this job */
        if (PMI_Get_size(&pmgr_nprocs) != PMI_SUCCESS) {
            pmgr_error("Getting number of ranks in job @ file %s:%d",
                __FILE__, __LINE__
            );
            PMI_Abort(1, "Failed to get number of ranks in job");
        }

        /* get jobid */
        if (PMI_Get_appnum(&pmgr_id) != PMI_SUCCESS) {
            pmgr_error("Getting job id @ file %s:%d",
                __FILE__, __LINE__
            );
            PMI_Abort(1, "Failed to get job id from PMI");
        }
#endif /* ifdef HAVE_PMI */
    }
    else if (mpirun_flux_cmb_enable) {
#ifdef HAVE_FLUX_CMB
        /* intialize the CMB context */
        comm_fab_cxt.cxt = (void *) cmb_init (); 
        if ( (cmb_t) comm_fab_cxt.cxt <= 0 ) {
            pmgr_error("cmb_init returned invalid context @ file %s:%d",
                __FILE__, __LINE__
            );
            exit (1);
        }

        /* rank and size has been harvest from the envVars already */

        /* assign size as the unique job id for now */
        pmgr_id = pmgr_nprocs;
#endif
    }

    /* =======================================================
     * Check that we have valid values
     * ======================================================= */

    /* MPIRUN_CLIENT_DEBUG={0,1} disables/enables debug statements */
    /* this comes *after* MPIRUN_RANK and MPIRUN_NPROCS since those are used to print debug messages */
    if ((value = pmgr_getenv("MPIRUN_CLIENT_DEBUG", ENV_OPTIONAL)) != NULL) {
        pmgr_echo_debug = atoi(value);
        int print_rank = 0;
        if (pmgr_echo_debug > 0) {
            if        (pmgr_echo_debug <= 1*PMGR_DEBUG_LEVELS) {
                print_rank = (pmgr_me == 0); /* just rank 0 prints */
            } else if (pmgr_echo_debug <= 2*PMGR_DEBUG_LEVELS) {
                print_rank = (pmgr_me == 0 || pmgr_me == pmgr_nprocs-1); /* just rank 0 and rank N-1 print */
            } else {
                print_rank = 1; /* all ranks print */
            }
            if (print_rank) {
                pmgr_echo_debug = 1 + (pmgr_echo_debug-1) % PMGR_DEBUG_LEVELS;
            } else {
                pmgr_echo_debug = 0;
            }
        }
    }

    /* check that we have a valid number of processes */
    if (pmgr_nprocs <= 0) {
        pmgr_error("Invalid NPROCS %s @ file %s:%d",
            pmgr_nprocs, __FILE__, __LINE__
        );
        exit(1);
    }

    /* check that our rank is valid */
    if (pmgr_me < 0 || pmgr_me >= pmgr_nprocs) {
        pmgr_error("Invalid RANK %s @ file %s:%d",
            pmgr_me, __FILE__, __LINE__
        );
        exit(1);
    }

    /* check that we have a valid jobid */
    if (pmgr_id == 0) {
        pmgr_error("Invalid JOBID %s @ file %s:%d",
            pmgr_id, __FILE__, __LINE__
        );
        exit(1);
    }

    /* set parameters */
    *np_p = pmgr_nprocs;
    *me_p = pmgr_me;
    *id_p = pmgr_id;

    pmgr_gettimeofday(&end);
    pmgr_debug(2, "Exiting pmgr_init(), took %f seconds for %d procs", pmgr_getsecs(&end,&start), pmgr_nprocs);
    return PMGR_SUCCESS;
}

/*
 * No cleanup necessary here.
 */
int pmgr_finalize()
{
    /* shut down the PMI library if we're using it */
    if (mpirun_pmi_enable) {
#ifdef HAVE_PMI
        if (PMI_Finalize() != PMI_SUCCESS) {
            pmgr_error("Failed to finalize PMI library @ file %s:%d",
                __FILE__, __LINE__
            );
        }
#endif /* ifdef HAVE_PMI */
    }

    pmgr_free(mpirun_hostname);
    return PMGR_SUCCESS;
}

/*
 * =============================
 * Handle aborts
 * =============================
 */

static int vprint_msg(char *buf, size_t len, const char *fmt, va_list ap)
{
    int n;

    n = vsnprintf(buf, len, fmt, ap);

    if ((n >= len) || (n < 0)) {
        /* Add trailing '+' to indicate truncation */
        buf[len - 2] = '+';
        buf[len - 1] = '\0';
    }

    return (0);
}

/*
 * Call into the process spawner, using the same port we were given
 * at startup time, to tell it to abort the entire job.
 */
int pmgr_abort(int code, const char *fmt, ...)
{
    int s;
    struct sockaddr_in sin;
    struct hostent* he;
    va_list ap;
    char buf [256];
    int len;

    /* if the tree is open, send out abort messages to parent and children */
    pmgr_abort_trees();

    /* build our error message */
    va_start(ap, fmt);
    vprint_msg(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* check whether we have an mpirun process, and check whether we can connect back to it */
    if (mpirun_hostname != NULL && !mpirun_pmi_enable && !(mpirun_shm_enable && pmgr_nprocs >= mpirun_shm_threshold)) {
        he = gethostbyname(mpirun_hostname);
        if (!he) {
            pmgr_error("pmgr_abort: Hostname lookup of mpirun failed (gethostbyname(%s) %s h_errno=%d) @ file %s:%d",
                mpirun_hostname, hstrerror(h_errno), h_errno, __FILE__, __LINE__
            );
            return -1;
        }

        s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            pmgr_error("pmgr_abort: Failed to create socket (socket() %m errno=%d) @ file %s:%d",
                errno, __FILE__, __LINE__
            );
            return -1;
        }

        memset(&sin, 0, sizeof(sin));
        sin.sin_family = he->h_addrtype;
        memcpy(&sin.sin_addr, he->h_addr_list[0], sizeof(sin.sin_addr));
        sin.sin_port = htons(mpirun_port);
        if (connect(s, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
            pmgr_error("pmgr_abort: Connect to mpirun failed (connect() %m errno=%d) @ file %s:%d",
                errno, __FILE__, __LINE__
            );
            return -1;
        }

        /* write an abort code (may be destination rank), our rank to mpirun */
        pmgr_write_fd(s, &code, sizeof(code));
        pmgr_write_fd(s, &pmgr_me, sizeof(pmgr_me));

        /* now length of error string, and error string itself to mpirun */
        len = strlen(buf) + 1;
        pmgr_write_fd(s, &len, sizeof(len));
        pmgr_write_fd(s, buf, len);

        close(s);
    } else { /* check that (mpirun_hostname != NULL) */
        /* TODO: want to echo this message here?  Want to do this for every user abort? */
        pmgr_error("Called pmgr_abort() Code: %d, Msg: %s", code, buf);
    }

    if (mpirun_pmi_enable) {
#ifdef HAVE_PMI
        PMI_Abort(code, buf);
#endif /* ifdef HAVE_PMI */
    }

    return PMGR_SUCCESS;
}

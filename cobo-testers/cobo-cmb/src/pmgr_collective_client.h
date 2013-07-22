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
 * This file defines the interface used by the MPI tasks (clients).
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

#ifndef _PMGR_COLLECTIVE_CLIENT_H
#define _PMGR_COLLECTIVE_CLIENT_H

/* enable C++ codes to include this header directly */
#ifdef __cplusplus
extern "C" {
#endif

#include "pmgr_collective_common.h"

/* operation types to use in pmgr_allreduce_int64t */
typedef enum pmgr_ops {
    PMGR_SUM = 1,
    PMGR_MAX
} pmgr_op;

/*
 * This function is called by each process in the job during
 * initialization.  Pointers to argc and argv are passes
 * in the event that the process manager passed args on
 * the command line.
 * The following values are filled in:
 *    *np_p     = total number of processes in the job
 *    *me_p     = the rank of this process (zero based)
 *    *id_p     = the global ID associated with this job.
 */
int pmgr_init(int *argc_p, char ***argv_p, int *np_p, int *me_p, int *id_p);

int pmgr_open ();

int pmgr_get_parent_socket (int *fd);

int pmgr_close();

int pmgr_finalize(void);

int pmgr_abort(int code, const char *fmt, ...);

/* sync point, no task makes it past until all have reached */
int pmgr_barrier  ();

/* execute an allreduce of int64_t types */
int pmgr_allreduce_int64t(int64_t* sendint, int64_t* recvint, pmgr_op op);

/* root sends sendcount bytes from buf, each task recevies sendcount bytes into buf */
int pmgr_bcast    (void* buf, int sendcount, int root);

/* each task sends sendcount bytes from buf, root receives N*sendcount bytes into recvbuf */
int pmgr_gather   (void* sendbuf, int sendcount, void* recvbuf, int root);

/* root sends blocks of sendcount bytes to each task indexed from sendbuf */
int pmgr_scatter  (void* sendbuf, int sendcount, void* recvbuf, int root);

/* each task sends sendcount bytes from sendbuf and receives N*sendcount bytes into recvbuf */
int pmgr_allgather(void* sendbuf, int sendcount, void* recvbuf);

/* each task sends N*sendcount bytes from sendbuf and receives N*sendcount bytes into recvbuf */
int pmgr_alltoall (void* sendbuf, int sendcount, void* recvbuf);

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
int pmgr_allgatherstr(char* sendstr, char*** recvstr, char** recvbuf);

/* collects data sent by each rank and writes at most max_recvcount bytes
 * into recvbuf.  sendcount may be different on each process, actual number
 * of bytes received provided as output in written.  data is *not* ordered
 * by rank, nor is it guaranteed to be received in the same order on each rank */
int pmgr_aggregate(const void* sendbuf, int64_t sendcount,
    void* recvbuf, int64_t recvcount, int64_t* written
);

/* enable C++ codes to include this header directly */
#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _PMGR_COLLECTIVE_CLIENT_H */

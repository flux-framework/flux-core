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

#include "pmgr_collective_common.h"
#include "pmgr_collective_client.h"
#include "pmgr_collective_client_common.h"
#include "pmgr_collective_client_mpirun.h"

/* parameters for connection attempts */
extern int mpirun_connect_tries;
extern int mpirun_connect_timeout;
extern int mpirun_connect_backoff;
extern int mpirun_connect_random;

/* records details on server process to bootstrap */
extern char* mpirun_hostname;
extern int   mpirun_port;

/* private variables to cache number of ranks in job and rank of calling process */
static int mpirun_ranks  = -1;
static int mpirun_rank   = -1;
static int mpirun_socket = -1;

/*
 * =============================
 * Read / write bytes to the mpirun socket
 * =============================
 */

/* read size bytes into buf from mpirun_socket */
static int pmgr_mpirun_read(void* buf, int size)
{
    int rc = 0;
    if ((rc = pmgr_read_fd(mpirun_socket, buf, size)) < 0) {
        pmgr_error("Reading from mpirun at %s:%d (read(buf=%x,size=%d) %m errno=%d) @ file %s:%d",
            mpirun_hostname, mpirun_port, buf, size, errno, __FILE__, __LINE__
        );
    }
    return rc;
}

/* write size bytes into mpirun_socket from buf */
static int pmgr_mpirun_write(void* buf, int size)
{
    int rc = 0;
    if ((rc = pmgr_write_fd(mpirun_socket, buf, size)) < 0) {
        pmgr_error("Writing to mpirun at %s:%d (write(buf=%x,size=%d) %m errno=%d) @ file %s:%d",
            mpirun_hostname, mpirun_port, buf, size, errno, __FILE__, __LINE__
        );
    }
    return rc;
}

/* write integer into mpirun_socket */
static int pmgr_mpirun_write_int(int value)
{
    int rc = pmgr_mpirun_write(&value, sizeof(value));
    return rc;
}

/*
 * =============================
 * Open, close mpirun socket
 * =============================
 */

int pmgr_mpirun_open(int ranks, int rank)
{
    if (mpirun_socket == -1) {
        /* cache number of ranks in job and our own rank */
        mpirun_ranks = ranks;
        mpirun_rank  = rank;

        /* open connection back to mpirun process */
        struct hostent* mpirun_hostent = gethostbyname(mpirun_hostname);
        if (!mpirun_hostent) {
            pmgr_error("Hostname lookup of mpirun failed (gethostbyname(%s) %s h_errno=%d) @ file %s:%d",
                mpirun_hostname, hstrerror(h_errno), h_errno, __FILE__, __LINE__
            );
            exit(1);
        }

        /* TODO: remove this hack, need it now so as not to overflow mpirun tcp listen queue */
        /* stagger connect attempts back to mpirun process based on MPI rank */
        usleep(mpirun_rank * 5);

        struct in_addr ip = *(struct in_addr *) *(mpirun_hostent->h_addr_list);
        mpirun_socket = pmgr_connect(ip, mpirun_port);
        if (mpirun_socket == -1) {
            pmgr_error("Connecting mpirun socket to %s at %s:%d failed @ file %s:%d",
                mpirun_hostent->h_name, inet_ntoa(ip), mpirun_port, __FILE__, __LINE__
            );
            exit(1);
        }

        /* we are now connected to the mpirun process */

        /* 
         * Exchange information with mpirun.  If you make any changes
         * to this protocol, be sure to increment the version number
         * in the header file.  This is to permit compatibility with older
         * executables.
         */

        /* send version number, then rank */
        pmgr_mpirun_write_int(PMGR_COLLECTIVE);
        pmgr_mpirun_write(&mpirun_rank, sizeof(mpirun_rank));
    }

    return PMGR_SUCCESS;
}

int pmgr_mpirun_close()
{
    if (mpirun_socket != -1) {
        /* TODO: remove this hack, need it now so as not to overflow mpirun tcp listen queue */
        /* stagger connect attempts back to mpirun process based on MPI rank */
        usleep(mpirun_rank * 5);

        /* send CLOSE op code, then close socket */
        pmgr_mpirun_write_int(PMGR_CLOSE);
        close(mpirun_socket);
        mpirun_socket = -1;
    }
    return PMGR_SUCCESS;
}

int pmgr_mpirun_is_open()
{
    return (mpirun_socket != -1);
}

/* 
 * =============================
 * The mpirun_* functions implement PMGR_COLLECTIVE operations through
 * the mpirun process.  Typically, this amounts to a flat tree with the
 * mpirun process at the root.  These functions implement the client side
 * of the protocol specified in pmgr_collective_mpirun.c.
 * =============================
 */

/*
 * Perform barrier, each task writes an int then waits for an int
 */
int pmgr_mpirun_barrier()
{
    /* send BARRIER op code, then wait on integer reply */
    int buf;

    if(mpirun_socket >= 0) {
        pmgr_mpirun_write_int(PMGR_BARRIER);
        pmgr_mpirun_read(&buf, sizeof(int));
    } else {
        pmgr_error("Barrier failed since socket to mpirun is not open @ %s:%d",
            __FILE__,__LINE__
        );
        return PMGR_FAILURE;
    }

    return PMGR_SUCCESS;
}

/*
 * Perform MPI-like Broadcast, root writes sendcount bytes from buf,
 * into mpirun_socket, all receive sendcount bytes into buf
 */
int pmgr_mpirun_bcast(void* buf, int sendcount, int root)
{
    if(mpirun_socket >= 0) {
        /* send BCAST op code, then root, then size of data */
        pmgr_mpirun_write_int(PMGR_BCAST);
        pmgr_mpirun_write_int(root);
        pmgr_mpirun_write_int(sendcount);

        /* if i am root, send data */
        if (mpirun_rank == root) {
            pmgr_mpirun_write(buf, sendcount);
        }

        /* read in data */
        pmgr_mpirun_read(buf, sendcount);
    } else {
        pmgr_error("Bcast failed since socket to mpirun is not open @ %s:%d",
            __FILE__,__LINE__
        );
        return PMGR_FAILURE;
    }

    return PMGR_SUCCESS;
}

/*
 * Perform MPI-like Gather, each task writes sendcount bytes from sendbuf
 * into mpirun_socket, then root receives N*sendcount bytes into recvbuf
 */
int pmgr_mpirun_gather(void* sendbuf, int sendcount, void* recvbuf, int root)
{
    if(mpirun_socket >= 0) {
        /* send GATHER op code, then root, then size of data, then data itself */
        pmgr_mpirun_write_int(PMGR_GATHER);
        pmgr_mpirun_write_int(root);
        pmgr_mpirun_write_int(sendcount);
        pmgr_mpirun_write(sendbuf, sendcount);

        /* only the root receives data */
        if (mpirun_rank == root) {
           pmgr_mpirun_read(recvbuf, sendcount * mpirun_ranks);
        }
    } else {
        pmgr_error("Gather failed since socket to mpirun is not open @ %s:%d",
            __FILE__,__LINE__
        );
        return PMGR_FAILURE;
    }

    return PMGR_SUCCESS;
}

/*
 * Perform MPI-like Scatter, root writes N*sendcount bytes from sendbuf
 * into mpirun_socket, then each task receives sendcount bytes into recvbuf
 */
int pmgr_mpirun_scatter(void* sendbuf, int sendcount, void* recvbuf, int root)
{
    if(mpirun_socket >= 0) {
        /* send SCATTER op code, then root, then size of data, then data itself */
        pmgr_mpirun_write_int(PMGR_SCATTER);
        pmgr_mpirun_write_int(root);
        pmgr_mpirun_write_int(sendcount);

        /* if i am root, send all chunks to mpirun */
        if (mpirun_rank == root) {
            pmgr_mpirun_write(sendbuf, sendcount * mpirun_ranks);
        }

        /* receive my chunk */
        pmgr_mpirun_read(recvbuf, sendcount);
    } else {
        pmgr_error("Scatter failed since socket to mpirun is not open @ %s:%d",
            __FILE__,__LINE__
        );
        return PMGR_FAILURE;
    }

    return PMGR_SUCCESS;
}

/*
 * Perform MPI-like Allgather, each task writes sendcount bytes from sendbuf
 * into mpirun_socket, then receives N*sendcount bytes into recvbuf
 */
int pmgr_mpirun_allgather(void* sendbuf, int sendcount, void* recvbuf)
{
    if(mpirun_socket >= 0) {
        /* send ALLGATHER op code, then size of data, then data itself */
        pmgr_mpirun_write_int(PMGR_ALLGATHER);
        pmgr_mpirun_write_int(sendcount);
        pmgr_mpirun_write(sendbuf, sendcount);
        pmgr_mpirun_read(recvbuf, sendcount * mpirun_ranks);
    } else {
        pmgr_error("Allgather failed since socket to mpirun is not open @ %s:%d",
            __FILE__,__LINE__
        );
        return PMGR_FAILURE;
    }

    return PMGR_SUCCESS;
}

/*
 * Perform MPI-like Alltoall, each task writes N*sendcount bytes from sendbuf
 * into mpirun_socket, then recieves N*sendcount bytes into recvbuf
 */
int pmgr_mpirun_alltoall(void* sendbuf, int sendcount, void* recvbuf)
{
    if(mpirun_socket >= 0) {
        /* send ALLTOALL op code, then size of data, then data itself */
        pmgr_mpirun_write_int(PMGR_ALLTOALL);
        pmgr_mpirun_write_int(sendcount);
        pmgr_mpirun_write(sendbuf, sendcount * mpirun_ranks);
        pmgr_mpirun_read(recvbuf, sendcount * mpirun_ranks);
    } else {
        pmgr_error("Alltoall failed since socket to mpirun is not open @ %s:%d",
            __FILE__,__LINE__
        );
        return PMGR_FAILURE;
    }

    return PMGR_SUCCESS;
}

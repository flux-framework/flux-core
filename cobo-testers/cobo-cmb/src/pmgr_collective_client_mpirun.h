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

#ifndef _PMGR_COLLECTIVE_CLIENT_MPIRUN_H
#define _PMGR_COLLECTIVE_CLIENT_MPIRUN_H

#include "pmgr_collective_client_mpirun.h"

/*
 * =============================
 * Open, close mpirun socket
 * =============================
 */

int pmgr_mpirun_open(int ranks, int rank);

int pmgr_mpirun_close();

int pmgr_mpirun_is_open();

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
int pmgr_mpirun_barrier();

/*
 * Perform MPI-like Broadcast, root writes sendcount bytes from buf,
 * into mpirun_socket, all receive sendcount bytes into buf
 */
int pmgr_mpirun_bcast(void* buf, int sendcount, int root);

/*
 * Perform MPI-like Gather, each task writes sendcount bytes from sendbuf
 * into mpirun_socket, then root receives N*sendcount bytes into recvbuf
 */
int pmgr_mpirun_gather(void* sendbuf, int sendcount, void* recvbuf, int root);

/*
 * Perform MPI-like Scatter, root writes N*sendcount bytes from sendbuf
 * into mpirun_socket, then each task receives sendcount bytes into recvbuf
 */
int pmgr_mpirun_scatter(void* sendbuf, int sendcount, void* recvbuf, int root);

/*
 * Perform MPI-like Allgather, each task writes sendcount bytes from sendbuf
 * into mpirun_socket, then receives N*sendcount bytes into recvbuf
 */
int pmgr_mpirun_allgather(void* sendbuf, int sendcount, void* recvbuf);

/*
 * Perform MPI-like Alltoall, each task writes N*sendcount bytes from sendbuf
 * into mpirun_socket, then recieves N*sendcount bytes into recvbuf
 */
int pmgr_mpirun_alltoall(void* sendbuf, int sendcount, void* recvbuf);

#endif /* _PMGR_COLLECTIVE_CLIENT_MPIRUN_H */


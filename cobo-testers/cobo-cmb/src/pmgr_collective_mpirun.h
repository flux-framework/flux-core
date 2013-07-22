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
 * This file defines the interface used by mpirun.  The mpirun process should call
 * pmgr_processops after accepting connections from the MPI tasks and negotiating
 * the protocol version number (PMGR_COLLECTIVE uses protocol 8).
 *
 * It should provide an array of open socket file descriptors indexed by MPI rank
 * (fds) along with the number of MPI tasks (nprocs) as arguments.
 *
 * pmgr_processops will handle all PMGR_COLLECTIVE operations and return control
 * upon an error or after receiving PMGR_CLOSE from the MPI tasks.  If no errors
 * are encountered, it will close all socket file descriptors before returning.
*/

#ifndef _PMGR_COLLECTIVE_MPIRUN_H
#define _PMGR_COLLECTIVE_MPIRUN_H

#include "pmgr_collective_common.h"

/*
  pmgr_processops
  This function carries out pmgr_collective operations to bootstrap MPI.
  These collective operations are modeled after MPI collectives -- all tasks
  must call them in the same order and with consistent parameters.

  fds - integer array of open sockets (file descriptors)
        indexed by MPI rank
  nprocs - number of MPI tasks in job

  returns PMGR_SUCCESS on success
  If no errors are encountered, all sockets are closed before returning.
*/
int pmgr_processops(int* fds, int nprocs);

#endif /* _PMGR_COLLECTIVE_MPIRUN_H */

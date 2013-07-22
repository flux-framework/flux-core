/*
 * Copyright (c) 2009, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory
 * Written by Adam Moody <moody20@llnl.gov>
 * LLNL-CODE-411039
 * All rights reserved.
 * This file is part of the PMGR_COLLECTIVE library.
 * For details, see https://sourceforge.net/projects/pmgrcollective.
 * Please also read this file: LICENSE.TXT.
*/

====================================================
PMGR_COLLECTIVE Overview
====================================================
PMGR_COLLECTIVE was motivated by two design goals:
  1) To provide flexibility in the MPI bootstapping.
  2) To provide scalable startup.

This library enables MPI to bootstrap itself through a series of collective
operations.  The collective operations are modeled after MPI collectives --
all tasks must call them in the same order and with consistent parameters.
MPI may invoke any number of collectives, in any order, passing an arbitrary
amount of data.  All message sizes are specified in bytes.

The library interface is defined in pmgr_collective_client.h.

An MPI task should make calls in the following sequenece:
  pmgr_init
  pmgr_open
  [pmgr_collectives]
  pmgr_close
  pmgr_finalize
All functions return PMGR_SUCCESS on successful completion.

In addition, the job launcher must provide some functionality.
It must either implement the collective protocol defined
in pmgr_collective_mpirun, or it must provide a PMI library.
As a convenience, we implement a simple (non-scalable) interface
in pmgr_collective_mpirun that can be called from the job launcher.

pmgr_collective_mpirun - called by mpirun
  The mpirun process should call pmgr_processops after accepting connections
  from the MPI tasks and negotiating the protocol version number (PMGR_COLLECTIVE
  uses protocol 8).

  It should provide an array of open socket file descriptors indexed by MPI rank
  (fds) along with the number of MPI tasks (nprocs) as arguments.

  pmgr_processops will handle all PMGR_COLLECTIVE operations and return control
  upon an error or after receiving PMGR_CLOSE from the MPI tasks.  If no errors
  are encountered, it will close all socket file descriptors before returning.
  It returns PMGR_SUCCESS on successful completion.

For large systems, one should implement a more scalable mechanism
than this simple implementation, or one should provide an efficient
implementation of PMI.

====================================================
Environment Variables for mpirun
====================================================
All environment variables are read when mpirun calls pmgr_processops().

Debug statements can be turned on for troubleshooting or timing:

  MPIRUN_DEBUG={0,1,2}
    0 - disable debug statements
    1 - first debug verbosity, includes timing
    2 - increased debug verbosity

====================================================
Environment Variables for the Client
====================================================
All environment variables are read when a client calls pmgr_init().

If the PMI library is not used, the following variables must be
set to define the client's MPI context:

  MPIRUN_NPROCS - number of MPI processes in job       must be >0
  MPIRUN_RANK   - the client's MPI rank in the job     must be in [0,N-1]
  MPIRUN_ID     - unique jobid of current application  must be !=0
  MPIRUN_HOST   - IP address of the mpirun process in dotted decimal notation
  MPIRUN_PORT   - port number of the mpirun process    must be >0

The following variables may be set to configure the connection to
the mpirun task:

  MPIRUN_CONNECT_TIMEOUT - number of seconds before connect attempt
                           times out (default 2)
  MPIRUN_CONNECT_BACKOFF - maximum number of seconds to wait before
                           retrying connect (default 5)
  MPIRUN_CONNECT_TRIES   - number of times to attempt connect before
                           aborting (default 7)
  MPIRUN_CONNECT_RANDOM  - enable/disable randomized backoff option
                           (default 1)
                           enabled (1) - wait a random amout of time
                             up to a max of the backoff time before
                             re-attempting connect
                           disabled (0) - always wait exactly the
                             backoff time before re-attempting connect

The above defaults may be changed by defining the appropriate macros
at compile time, for example:

  -DMPIRUN_CONNECT_TIMEOUT=5
  -DMPIRUN_CONNECT_BACKOFF=10
  -DMPIRUN_CONNECT_TRIES=20
  -DMPIRUN_CONNECT_RANDOM=1

To enable support for the PMI library, define the following at compile time:

  -DHAVE_PMI

Even when support for the PMI library is compiled in, calls to the PMI
library are disabled by default.  To force PMGR_COLLECTIVE to use PMI,
set the following variable at run time:

  MPIRUN_USE_PMI=1

One can configure PMGR_COLLECTIVE to use PMI by default by defining
the following at compile time:

  -DHAVE_PMI -DMPIRUN_PMI_ENABLE=1
 
Debug statements can be turned on for troubleshooting or timing.  There are
three basic debug verbosity levels.  For each of these, there is a way to
have just rank 0 print, rank 0 and rank N-1 print, or have all print.

  MPIRUN_CLIENT_DEBUG={0-6}
    0                     - disable debug statements
  Rank 0 only:
    1 - includes pmgr_open to pmgr_close timing
    2 - includes timing for each pmgr_* function
    3 - denotes start and exit for each pmgr_* function, and prints env var
  Rank 0 and rank N-1 only:
    4 - includes pmgr_open to pmgr_close timing
    5 - includes timing for each pmgr_* function
    6 - denotes start and exit for each pmgr_* function, and prints env var
  All ranks:
    7 - includes pmgr_open to pmgr_close timing
    8 - includes timing for each pmgr_* function
    9 - denotes start and exit for each pmgr_* function, and prints env var

====================================================
Current Implementation
====================================================
The current implementation sets up a binomial tree via TCP sockets with rank 0 at the root.
During pmgr_open(), each client process opens a TCP socket to accept connections and passes
its IP:port to mpirun.  After gathering the IP:port entry for each process, mpirun passes
this socket table to rank 0.  Rank 0 then connects to each of its children (now that it has
everyone's address) and forwards the table to each.  Each child receives the socket table,
opens a connection with any children it has, and forwards the table.  The process continues
down the tree until a leaf child is reached which just accepts the incoming parent connection.

This same binomial tree, which was used to distribute the socket table, is also used to gather,
scatter, and broadcast data during collective operations.  Namely, this tree is used to provide
log(N) scalable algorithms for:

  pmgr_barrier
  pmgr_allgather
  pmgr_allgatherstr
  pmgr_bcast   (when rank 0 is the root)
  pmgr_gather  (when rank 0 is the root)
  pmgr_scatter (when rank 0 is the root)

The tree is also used for alltoall, although it's not a log(N) algorithm.  Basically,
the alltoall is implemented as a gather to rank 0 (with data reordering) followed by
a scatter from rank 0.

  pmgr_alltoall

The remaining collectives are implemented via mpirun support, which typically amounts to a
flat tree with mpirun as the root.  These collectives thus scale linearly:

  pmgr_bcast   (when rank 0 is not the root)
  pmgr_gather  (when rank 0 is not the root)
  pmgr_scatter (when rank 0 is not the root)

This implementation works best if the TCP socket each task opens corresponds to a port
on a switched network so that multiple nodes may send and receive data at the same time.

It's possible to disable / enable the binomial tree at runtime:

  MPIRUN_USE_TREES={0,1}        - disable/enable binomial tree for all operations (also avoids setup and teardown)

If the trees are disabled, then the collective will fall back to the mpirun
support, which is typically a flat tree with the mpirun process at the root.

By default, the tree is enabled by default.  The library can be built with the tree disabled
by default.  To do this, include the following in your compile flags:

  -DMPIRUN_USE_TREES=0

====================================================
Ideas for Future Work (if the demand arises)
====================================================
It's possible to extend this implementation to set set up other TCP topograhies (in addition
to the binomial tree) via the socket table to provide scalable support for other collectives.

A scalable PMI could probably be implemented on top of PMGR_COLLECTIVE using pmgr_allgather.

Reliability could be added to retry or reestablish connections.

As process counts increase higher and higher, at some point, mpirun itself will likely need
to collect the IP:port entries via a tree network to build the socket table.  This could be
done transparently from PMGR_COLLECTIVES point of view.

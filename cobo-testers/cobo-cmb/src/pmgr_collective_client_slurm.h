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

/* This implementation uses SLURM environment variables and shared memory
 * to construct the table very quickly. */

#ifndef _PMGR_COLLECTIVE_CLIENT_SLURM_H
#define _PMGR_COLLECTIVE_CLIENT_SLURM_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "pmgr_collective_client_tree.h"

/* given number of ranks, our rank, our ip address, and our port,
 * build and return table of all ip addresses and ports in job */
int pmgr_tree_open_slurm(pmgr_tree_t* t, int ranks, int rank, const char* auth);

#endif /* _PMGR_COLLECTIVE_CLIENT_SLURM_H */

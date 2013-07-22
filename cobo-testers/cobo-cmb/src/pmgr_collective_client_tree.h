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

#ifndef _PMGR_COLLECTIVE_CLIENT_TREE_H
#define _PMGR_COLLECTIVE_CLIENT_TREE_H

#include "pmgr_collective_common.h"

#define PMGR_GROUP_TREE_NULL     (0)
#define PMGR_GROUP_TREE_BINOMIAL (1)

#if 0
typedef struct pmgr_tree_endpoint {
    int rank;
    int fd;
    struct in_addr ip;
    unsigned short port;
    char* host;
    char* name;
} pmgr_tree_endpoint_t;
#endif

typedef struct pmgr_tree {
    int type;      /* type of group */
    int ranks;     /* number of ranks in group */
    int rank;      /* rank of process within group */
    char* host;    /* name of host the process is on */
    char* name;    /* name of process */
    int is_open;   /* records whether group is connected */
    int depth;     /* depth within the tree */
    int parent_rank;          /* rank of parent within group */
    char* parent_host;        /* name of host parent is running on */
    int parent_fd;            /* socket to parent */
    struct in_addr parent_ip; /* ip address of parent */
    short parent_port;        /* port of parent */
    char* parent_name;        /* user-friendly name to print in error messages */
    int num_child; /* number of children this process has */
    int num_child_incl; /* total number of procs below parent (including itself) */
    int*            child_rank; /* rank of each child within group */
    char**          child_host; /* name of host child is running on */
    int*            child_fd;   /* file descriptor to each child */
    int*            child_incl; /* number of procs each child is responsible for */
    struct in_addr* child_ip;   /* ip address of each child */
    short*          child_port; /* port of each child */
    char**          child_name; /* user-friendly name to print in error messages */
} pmgr_tree_t;

/*
 * =============================
 * This function is implemented in pmgr_collective_client.c
 * where the necessary variables are defined, but it is called
 * from many files dealing with trees.
 * =============================
 */

/* abort all open trees */
int pmgr_abort_trees();

/*
 * =============================
 * Initialize and free tree data structures
 * =============================
 */

/* initialize tree to null tree */
int pmgr_tree_init_null(pmgr_tree_t* t);

/* given number of ranks and our rank with the group, create a binomail tree
 * fills in our position within the tree and allocates memory to hold socket info */
int pmgr_tree_init_binomial(pmgr_tree_t* t, int ranks, int rank);

/* given number of ranks and our rank with the group, create a binary tree
 * fills in our position within the tree and allocates memory to hold socket info */
int pmgr_tree_init_binary(pmgr_tree_t* t, int ranks, int rank);

/* free all memory allocated in tree */
int pmgr_tree_free(pmgr_tree_t* t);

/*
 * =============================
 * Open, close, and abort trees
 * =============================
 */

/* returns 1 if tree is open, 0 otherwise */
int pmgr_tree_is_open(pmgr_tree_t* t);

/* close down socket connections for tree (parent and any children),
 * free memory for tree data structures */
int pmgr_tree_close(pmgr_tree_t* t);

/* send abort message across links, then close down tree */
int pmgr_tree_abort(pmgr_tree_t* t);

/* check whether all tasks report success, exit if someone failed */
int pmgr_tree_check(pmgr_tree_t* t, int value);

/* open socket tree across MPI tasks */
int pmgr_tree_open(pmgr_tree_t* t, int ranks, int rank, const char* auth);

/* given a table of ranks number of ip:port entries, open the tree */
int pmgr_tree_open_table(pmgr_tree_t* t, int ranks, int rank, const void* table, int sockfd, const char* auth);

/* given a table of ranks number of ip:port entries, open a tree */
int pmgr_tree_open_nodelist_scan(pmgr_tree_t* t, const char* nodelist, const char* portrange, int portoffset, int sockfd, const char* auth);

/*
 * =============================
 * Colletive implementations over tree
 * =============================
 */

/* broadcast size bytes from buf on rank 0 using socket tree */
int pmgr_tree_bcast(pmgr_tree_t* t, void* buf, int size);

/* gather sendcount bytes from sendbuf on each task into recvbuf on rank 0 */
int pmgr_tree_gather(pmgr_tree_t* t, void* sendbuf, int sendcount, void* recvbuf);

/* scatter sendcount byte chunks from sendbuf on rank 0 to recvbuf on each task */
int pmgr_tree_scatter(pmgr_tree_t* t, void* sendbuf, int sendcount, void* recvbuf);

/* computes maximum integer across all processes and saves it to recvbuf on rank 0 */
int pmgr_tree_allreduce_int64t(pmgr_tree_t* t, int64_t* sendint, int64_t* recvint, pmgr_op op);

/* collects all data from all tasks into recvbuf which is at most max_recvcount bytes big,
 * effectively works like a gatherdv */
int pmgr_tree_aggregate(pmgr_tree_t*, const void* sendbuf, int64_t sendcount, void* recvbuf, int64_t recvcount, int64_t* written);

/* alltoall sendcount bytes from each process to each process via tree */
int pmgr_tree_alltoall(pmgr_tree_t* t, void* sendbuf, int sendcount, void* recvbuf);

#endif /* _PMGR_COLLECTIVE_CLIENT_TREE_H */

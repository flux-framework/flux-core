/*
 * ==========================================================================
 * This library enables distributed processes to bootstrap themselves through
 * a series of collective operations. The collective operations are modeled
 * after MPI collectives -- all tasks must call them in the same order and with
 * consistent parameters.
 *
 * Any number of collectives may be invoked, in any order, passing an arbitrary
 * amount of data. All message sizes are specified in bytes.
 *
 * All functions return COBO_SUCCESS on successful completion.
 * ==========================================================================
 */

#ifndef _COBO_FEN_H
#define _COBO_FEN_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "pmgr_collective_common.h"
#include "pmgr_collective_client_common.h"
#include "pmgr_collective_client.h"

#ifndef COBO_SUCCESS
# define COBO_SUCCESS (0)
# define COBO_FAILURE (-1)
#endif


/**
 * Defines a data type to store connection info for the tree root
 *
 */
typedef struct _conn_2_tree_t {
    int sockFd;           /*!< socket file descriptor (init: -1)*/
    struct in_addr remIp; /*!< IP address of the root back-end */
    short remPort;        /*!< port number of the connection */  
} conn_2_tree_t;


/**
 * Opens COBO for the calling front-end. 
 *
 * @param[in] sockfd a opened socket of AF_INET that's 
 *                   performed "bind" and "listen" 
 * @param[out] sessid
 * @return COBO_SUCCESS on success; otherwise COBO_FAILURE
 * 
 */
extern int 
cobo_server_open (int sockfd, unsigned int *sessid);


/** 
 * Shut down the tree connections (leaves processes running) 
 *
 * @return COBO_SUCCESS on success; otherwise COBO_FAILURE 
 *
 */
extern int 
cobo_server_close ();


/**
 * Fills in fd with socket file desriptor 
 * to the root back-end (rank 0) 
 *
 * @param[out] fd filled with the socket file descriptor 
 *                to the root back-end
 * @return COBO_SUCCESS on success; otherwise COBO_FAILURE 
 *
 */
extern int 
cobo_server_get_root_socket (int* fd);

#endif /* _COBO_FEN_H */ 


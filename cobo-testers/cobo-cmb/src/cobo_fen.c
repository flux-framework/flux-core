/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Dong Ahn <ahn1@llnl.gov>
 *
 */

#include "cobo_fen.h" 


/******************************************************
 *
 *                    STATIC DATA
 *
 *****************************************************/ 
conn_2_tree_t myConnInfo = {
    .sockFd = -1, 
    .remIp = {0},
    .remPort = -1
};


/******************************************************
 *
 *                    PUBLIC FUNCTIONS 
 *
 *****************************************************/ 
int cobo_server_open (int sockfd, unsigned int *sessid)
{
    int rc;

    /*
     * Assume mysockfd has been through "bind" and "listen"
     * Caller must close the listening socket if it no 
     * longer needs to listen after this call.
     */ 
    rc = pmgr_accept (sockfd, 
                      NULL, 
                      &(myConnInfo.sockFd), 
                      &(myConnInfo.remIp),
                      &(myConnInfo.remPort)); 

    if (rc != PMGR_SUCCESS)
        goto ret_loc;
    
    
    rc = pmgr_read_fd (myConnInfo.sockFd, 
                       sessid, 
                       sizeof(*sessid));

    rc = (rc <= 0)? COBO_FAILURE : COBO_SUCCESS;

ret_loc:
    return rc;
}


int cobo_server_close ()
{
    int rc;
    rc = close (myConnInfo.sockFd);
    return (rc != -1)? COBO_SUCCESS : rc;
}


int cobo_server_get_root_socket (int *fd)
{
    (*fd) = myConnInfo.sockFd;
    return (*fd != -1)? COBO_SUCCESS : -1;
}


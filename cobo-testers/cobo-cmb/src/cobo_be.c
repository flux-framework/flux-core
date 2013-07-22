/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Dong Ahn <ahn1@llnl.gov>
 *
 */ 

#include "cobo_be.h" 

/******************************************************
 *
 *                    STATIC DATA
 *
 ******************************************************/
static be_personality_t personality = {
    .sockFd = -1,
    .rank = -1,
    .size = -1
};


/******************************************************
 *
 *                    PUBLIC FUNCTIONS 
 *
 *****************************************************/


int 
cobo_open (int *argc, char ***argv, 
	   const char *serv_ip, int port, 
           int *size, int *rank, unsigned int *sessid)
{
    int rc;
    struct in_addr serv_in_addr;
    
    rc = pmgr_init (argc, argv, size, rank, (int *) sessid); 
    if ( rc != PMGR_SUCCESS) {
        rc = COBO_FAILURE;
	goto ret_loc;
    }

    personality.rank = *rank;
    personality.size = *size;
    
    rc = pmgr_open ();
    if ( rc != PMGR_SUCCESS) {
        rc = COBO_FAILURE;
	goto ret_loc;
    }
  
    rc = cobo_get_parent_socket (&personality.sockFd);
    if ( rc != PMGR_SUCCESS) { 
        rc = COBO_FAILURE;
	goto ret_loc;
    }


    if ( *rank == 0) {
        /*
         * Special handling for the tree root back-end
         */  
        rc = inet_pton (AF_INET, 
                        serv_ip,
                        (void *) &serv_in_addr);
        rc = (rc < 0) ? COBO_FAILURE : COBO_SUCCESS;
        if ( rc == COBO_FAILURE ) 
            goto ret_loc;

        personality.sockFd = pmgr_connect (serv_in_addr, port);
        if ( personality.sockFd < 0) {
            rc = COBO_FAILURE;
            goto ret_loc;
        }
        rc = pmgr_authenticate_connect (personality.sockFd,
                                        NULL,
                                        NULL,
                                        60000);
        if ( rc != PMGR_SUCCESS ) {
            rc = COBO_FAILURE;
	    goto ret_loc;
        }
       rc = pmgr_write_fd (personality.sockFd, 
                           sessid, sizeof(*sessid));
       rc = (rc < 0) ? COBO_FAILURE : COBO_SUCCESS;
    }

ret_loc:
    return rc;  
}


int 
cobo_close ()
{
    return pmgr_close ();
}


int
cobo_get_parent_socket (int *fd)
{
    int rc = COBO_SUCCESS;
    if (personality.sockFd != -1) {
        *fd = personality.sockFd;
    }
    else {
        rc = pmgr_get_parent_socket (fd);
    }

    return rc; 
}


int 
cobo_barrier()
{
    return pmgr_barrier ();
}


int 
cobo_bcast (void* buf, int sendcount, int root)
{
    return pmgr_bcast (buf, sendcount, root);
}


int 
cobo_gather (void* sendbuf, int sendcount, 
	     void* recvbuf, int root)
{
    return pmgr_gather (sendbuf, sendcount, recvbuf, root);
}


int 
cobo_scatter (void* sendbuf, int sendcount, 
	      void* recvbuf, int root)
{
    return pmgr_scatter (sendbuf, sendcount, recvbuf, root);
}


int 
cobo_allgather(void* sendbuf, int sendcount, 
	       void* recvbuf)
{
    return pmgr_allgather (sendbuf, sendcount, recvbuf);
}


int 
cobo_alltoall (void* sendbuf, int sendcount, 
	       
	       void* recvbuf)
{
    return pmgr_alltoall (sendbuf, sendcount, recvbuf);
}


int 
cobo_allgatherstr (char* sendstr, char*** recvstr, 
		   char** recvbuf)
{
    return pmgr_allgatherstr (sendstr, recvstr, recvbuf);
}


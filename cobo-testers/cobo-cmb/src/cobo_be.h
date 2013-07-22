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

#ifndef _COBO_BE_H
#define _COBO_BE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include "pmgr_collective_common.h"
#include "pmgr_collective_client_common.h"
#include "pmgr_collective_client.h"

#ifndef COBO_SUCCESS
# define COBO_SUCCESS (0)
# define COBO_FAILURE (-1)
#endif

/**
 * Defines a back-end pesonality type 
 *
 */
typedef struct _be_personality_t {
    int sockFd; /*!< socket file descriptor to the parent (init: -1)*/
    int rank;   /*!< my rank */
    int size;   /*!< number of back-ends */
} be_personality_t;


/**
 * Opens COBO back-ends. All back-ends must call this together. 
 * It uses PMRG to open a TCP-tree-based network via which back-ends
 * can make use of various collective call. Finally, this function
 * also connects to the server that would have been listening on 
 * serv_ip:port at the end.
 *
 * @param[in,out] argcptr pointer to the command-line argc 
 * @param[in,out] argvptr pointer to the command-line argv
 * @param[in] serv_ip IP address of the front-end's listening socket
 * @param[in] port port number of the front-end's listening socket
 * @param[out] size the number of back-ends that called this
 * @param[out] rank an unique rank assigned to the calling back-end
 * @param[out] sessid an unique session id assign to this back-end tree
 * @return COBO_SUCCESS on success; otherwise COBO_FAILURE
 *
 */
extern int 
cobo_open (int *argcptr, char ***argvptr, const char *serv_ip, int port, 
           int *size, int *rank, unsigned int *sessid);


/** 
 * Shuts down the connections between tasks and free data structures 
 *
 * @return COBO_SUCCESS on success; otherwise COBO_FAILURE
 *
 */
extern int 
cobo_close ();


/** 
 * Fills in fd with socket file descriptor to the calling back-end's parent 
 * in the tree.
 *
 * @param[out] fd the socket file descriptor of the calling back-end's parent 
 * @return COBO_SUCCESS on success; otherwise COBO_FAILURE
 * 
 */
extern int 
cobo_get_parent_socket (int *fd);


/**
 * Sync point, no task makes it past until all have reached 
 *
 * @return COBO_SUCCESS on success; otherwise COBO_FAILURE
 *
 */
extern int 
cobo_barrier ();


/**
 * Root sends sendcount bytes from buf, each back-end recevies 
 * sendcount bytes into buf. 
 *
 * @param[in,out] buf a pointer to the buf to send and broadcast
 * @param[in] sendcount number of bytes to send
 * @param[in] root the rank of the initial back-end that send the buf
 * @return COBO_SUCCESS on success; otherwise COBO_FAILURE
 *
 */
extern int 
cobo_bcast (void* buf, int sendcount, int root);


/**
 * Each back-end sends sendcount bytes from buf, 
 * root receives N*sendcount bytes into recvbuf.
 *
 * @param[in] sendbuf a pointer to the buffer to be gathered (only meaningful to root)
 * @param[in] sendcount the number of bytes to send from sendbuf
 * @param[out] recvbuf a pointer to the buffer to receive the gathered buffers
 * @param[in] root the rank of the receiving back-end
 * @return COBO_SUCCESS on success; otherwise COBO_FAILURE
 *
 */
extern int 
cobo_gather (void* sendbuf, int sendcount, void* recvbuf, int root);


/**
 * Root sends blocks of sendcount bytes to each task indexed 
 * from sendbuf.
 *
 * @param[in] sendbuf a pointer to the buffer to be scattered (only meaningful to root)
 * @param[in] sendcount the number of bytes to send from sendbuf
 * @param[out] recvbuf a pointer to the buffer to receive the scattered blocks
 * @param[in] root the rank of the sending back-end
 * @return COBO_SUCCESS on success; otherwise COBO_FAILURE
 *
 */
extern int 
cobo_scatter (void* sendbuf, int sendcount, void* recvbuf, int root);


/** 
 * Each task sends sendcount bytes from sendbuf and 
 * receives N*sendcount bytes into recvbuf.
 *
 * @param[in] sendbuf the starting address of the send buffer 
 * @param[in] sendcount the number of bytes to send from sendbuf
 * @param[out] recvbuf the starting address of the recv buffer 
 * @return COBO_SUCCESS on success; otherwise COBO_FAILURE
 *
 */
extern int 
cobo_allgather (void* sendbuf, int sendcount, void* recvbuf);


/**
 * Each task sends N*sendcount bytes from sendbuf and 
 * receives N*sendcount bytes into recvbuf 
 *
 * @param[in] sendbuf the staring address of the send buffer 
 * @param[in] sendcount the number of bytes to send to each back-end
 * @param[out] recvbuf the starting address to the recv buffer
 * @return COBO_SUCCESS on success; otherwise COBO_FAILURE
 *
 */
extern int 
cobo_alltoall (void* sendbuf, int sendcount, void* recvbuf);


/**
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
 * char host[256], **hosts, *buf;
 * gethostname(host, sizeof(host));
 * cobo_allgatherstr(host, &hosts, &buf);
 * for(int i=0; i<nprocs; i++) { printf("rank %d runs on host %s\n", i, hosts[i]); }
 * free(hosts);
 * free(buf);
 *
 * @param[in] sendstr the send string 
 * @param[out] recvstr the array of pointers to strings 
 * @param[out] recvbuf the starting address of the gathered string data 
 * @return COBO_SUCCESS on success; otherwise COBO_FAILURE
 */
extern int 
cobo_allgather_str (char* sendstr, char*** recvstr, char** recvbuf);


extern double __cobo_ts;

#endif /* _COBO_CLIENT_H */ 


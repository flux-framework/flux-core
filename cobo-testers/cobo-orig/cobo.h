/*
 * ==========================================================================
 * This library enables distributed processes to bootstrap themselves through
 * a series of collective operations.  The collective operations are modeled
 * after MPI collectives -- all tasks must call them in the same order and with
 * consistent parameters.
 *
 * Any number of collectives may be invoked, in any order, passing an arbitrary
 * amount of data.  All message sizes are specified in bytes.
 *
 * All functions return COBO_SUCCESS on successful completion.
 * ==========================================================================
*/

#ifndef _COBO_H
#define _COBO_H

#define COBO_SUCCESS (0)

/*
 * ==========================================================================
 * ==========================================================================
 * Client Interface Functions
 * ==========================================================================
 * ==========================================================================
 */

/* provide list of ports and number of ports as input, get number of tasks and my rank as output */
int cobo_open(unsigned int sessionid, int* portlist, int num_ports, int* rank, int *num_ranks);

/* shut down the connections between tasks and free data structures */
int cobo_close();

/* fills in fd with socket file desriptor to our parent */
/* TODO: the upside here is that the upper layer can directly use our
 * communication tree, but the downside is that it exposes the implementation
 * and forces sockets */
int cobo_get_parent_socket(int* fd);

/* sync point, no task makes it past until all have reached */
int cobo_barrier();

/* root sends sendcount bytes from buf, each task recevies sendcount bytes into buf */
int cobo_bcast    (void* buf, int sendcount, int root);

/* each task sends sendcount bytes from buf, root receives N*sendcount bytes into recvbuf */
int cobo_gather   (void* sendbuf, int sendcount, void* recvbuf, int root);

/* root sends blocks of sendcount bytes to each task indexed from sendbuf */
int cobo_scatter  (void* sendbuf, int sendcount, void* recvbuf, int root);

/* each task sends sendcount bytes from sendbuf and receives N*sendcount bytes into recvbuf */
int cobo_allgather(void* sendbuf, int sendcount, void* recvbuf);

/* each task sends N*sendcount bytes from sendbuf and receives N*sendcount bytes into recvbuf */
int cobo_alltoall (void* sendbuf, int sendcount, void* recvbuf);

/*
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
 *   char host[256], **hosts, *buf;
 *   gethostname(host, sizeof(host));
 *   cobo_allgatherstr(host, &hosts, &buf);
 *   for(int i=0; i<nprocs; i++) { printf("rank %d runs on host %s\n", i, hosts[i]); }
 *   free(hosts);
 *   free(buf);
 */
int cobo_allgather_str(char* sendstr, char*** recvstr, char** recvbuf);

/*
 * ==========================================================================
 * ==========================================================================
 * Server Interface Functions
 * ==========================================================================
 * ==========================================================================
 */

/* given a hostlist and portlist where clients are running, open the tree and assign ranks to clients */
int cobo_server_open(unsigned int sessionid, char** hostlist, int num_hosts, int* portlist, int num_ports);

/* shut down the tree connections (leaves processes running) */
int cobo_server_close();

/* fills in fd with socket file desriptor to the root client process (rank 0) */
/* TODO: the upside here is that the upper layer can directly use our
 * communication tree, but the downside is that it exposes the implementation
 * and forces sockets */
int cobo_server_get_root_socket(int* fd);

extern double __cobo_ts;

#endif /* _COBO_H */

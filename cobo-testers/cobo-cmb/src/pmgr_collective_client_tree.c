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

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include "pmgr_collective_ranges.h"
#include "pmgr_collective_client.h"
#include "pmgr_collective_client_common.h"
#include "pmgr_collective_client_mpirun.h"
#include "pmgr_collective_client_tree.h"
#include "pmgr_collective_client_slurm.h"

#ifdef HAVE_PMI
#include "pmi.h"
#endif

#ifdef HAVE_FLUX_CMB
typedef int bool;
#include <json/json.h>
#include "cmb.h"
#endif

/* packet headers for messages in tree */
#define PMGR_TREE_HEADER_ABORT      (0)
#define PMGR_TREE_HEADER_COLLECTIVE (1)

extern int mpirun_pmi_enable;
extern int mpirun_flux_cmb_enable;
extern int mpirun_shm_enable;
extern int mpirun_shm_threshold;
extern int mpirun_authenticate_timeout;
extern int mpirun_connect_down;

/* construct name in format "rank %d of %d" */
static int pmgr_tree_build_name_rank(int rank, int ranks, char** out_name)
{
    /* free existing string if any */
    if (*out_name != NULL) {
        free(*out_name);
        *out_name = NULL;
    }

    char name_format[] = "rank %d of %d";

    /* get number of bytes, be sure to include a byte for trailing NUL */
    int name_len = snprintf(NULL, 0, name_format, rank, ranks) + 1;

    /* allocate memory */
    char* name = NULL;
    if (name_len > 0) {
        name = (char*) malloc(name_len);
    }

    /* check that we allocated memory ok */
    if (name == NULL) {
        return PMGR_FAILURE;
    }

    /* write the format */
    snprintf(name, name_len, name_format, rank, ranks);

    /* save newly allocated string in user's pointer */
    *out_name = name;

    return PMGR_SUCCESS;
}

static int pmgr_tree_build_name_child_rank(pmgr_tree_t* t, int i)
{
    /* build the new name */
    pmgr_tree_build_name_rank(t->child_rank[i], t->ranks, &(t->child_name[i]));

    return PMGR_SUCCESS;
}

static int pmgr_tree_build_name_parent_rank(pmgr_tree_t* t)
{
    /* build the new name */
    pmgr_tree_build_name_rank(t->parent_rank, t->ranks, &(t->parent_name));

    return PMGR_SUCCESS;
}

/* construct name in format "rank %d of %d at IP:port" */
static int pmgr_tree_build_name_ip(int rank, int ranks, struct in_addr ip, short port, const char* host, char** out_name)
{
    /* free existing string if any */
    if (*out_name != NULL) {
        free(*out_name);
        *out_name = NULL;
    }

    char name_without_host[] = "rank %d of %d at %s:%u";
    char name_with_host[]    = "rank %d of %d at %s:%u on %s";

    /* get number of bytes, be sure to include a byte for trailing NUL */
    int name_len = 0;
    if (host == NULL) {
        name_len = snprintf(NULL, 0, name_without_host, rank, ranks, inet_ntoa(ip), (unsigned short) port) + 1;
    } else {
        name_len = snprintf(NULL, 0, name_with_host, rank, ranks, inet_ntoa(ip), (unsigned short) port, host) + 1;
    }

    /* allocate memory */
    char* name = NULL;
    if (name_len > 0) {
        name = (char*) malloc(name_len);
    }

    /* check that we allocated memory ok */
    if (name == NULL) {
        return PMGR_FAILURE;
    }

    /* write the format */
    if (host == NULL) {
        snprintf(name, name_len, name_without_host, rank, ranks, inet_ntoa(ip), (unsigned short) port);
    } else {
        snprintf(name, name_len, name_with_host, rank, ranks, inet_ntoa(ip), (unsigned short) port, host);
    }

    /* save newly allocated string in user's pointer */
    *out_name = name;

    return PMGR_SUCCESS;
}

static int pmgr_tree_build_child_name_ip(pmgr_tree_t* t, int i)
{
    /* build the new name */
    pmgr_tree_build_name_ip(t->child_rank[i], t->ranks, t->child_ip[i], t->child_port[i], t->child_host[i], &(t->child_name[i]));

    return PMGR_SUCCESS;
}

static int pmgr_tree_build_parent_name_ip(pmgr_tree_t* t)
{
    /* build the new name */
    pmgr_tree_build_name_ip(t->parent_rank, t->ranks, t->parent_ip, t->parent_port, t->parent_host, &(t->parent_name));

    return PMGR_SUCCESS;
}

/* write an abort packet across socket */
static int pmgr_write_abort(int fd)
{
    /* just need to write the integer code for an abort message */
    int header = PMGR_TREE_HEADER_ABORT;
    int rc = pmgr_write_fd(fd, &header, sizeof(int));
    if (rc < sizeof(int)) {
        /* the write failed, close this socket, and return the error */
        return rc;
    }
    return rc;
}

/* write a collective packet across socket */
static int pmgr_write_collective(pmgr_tree_t* t, int fd, void* buf, int size)
{
    int rc = 0;

    /* check that size is positive */
    if (size <= 0) {
        return size;
    }

    /* first write the integer code for a collective message */
    int header = PMGR_TREE_HEADER_COLLECTIVE;
    rc = pmgr_write_fd(fd, &header, sizeof(int));
    if (rc < sizeof(int)) {
        /* the write failed, close the socket, and return an error */
        pmgr_error("Failed to write collective packet header rc=%s @ file %s:%d",
            pmgr_errstr(rc), __FILE__, __LINE__
        );
        return rc;
    }

    /* now write the data for this message */
    rc = pmgr_write_fd(fd, buf, size);
    if (rc < size) {
        /* the write failed, close the socket, and return an error */
        pmgr_error("Failed to write collective packet data rc=%s @ file %s:%d",
            pmgr_errstr(rc), __FILE__, __LINE__
        );
        return rc;
    }

    return rc;
}

/* receive a collective packet from socket */
static int pmgr_read_collective(pmgr_tree_t* t, int fd, void* buf, int size)
{
    int rc = 0;

    /* check that size is positive */
    if (size <= 0) {
        return size;
    }

    /* check that we have a buffer */
    if (buf == NULL) {
        return -1;
    }

    /* read the packet header */
    int header = 1;
    rc = pmgr_read_fd(fd, &header, sizeof(int));
    if (rc <= 0) {
        /* failed to read packet header, print error, close socket, and return error */
        pmgr_error("Failed to read packet header rc=%s @ file %s:%d",
            pmgr_errstr(rc), __FILE__, __LINE__
        );
        return rc;
    }

    /* process the packet */
    if (header == PMGR_TREE_HEADER_COLLECTIVE) {
        /* got our collective packet, now read its data */
        rc = pmgr_read_fd(fd, buf, size);
        if (rc <= 0) {
            /* failed to read data from socket, print error, close socket, and return error */
            pmgr_error("Failed to read collective packet data rc=%s @ file %s:%d",
                pmgr_errstr(rc), __FILE__, __LINE__
            );
            return rc;
        }
    } else if (header == PMGR_TREE_HEADER_ABORT) {
        /* received an abort packet, close the socket this packet arrived on,
         * broadcast an abort packet and exit with success */
        pmgr_tree_abort(t);
        pmgr_abort_trees();
        exit(0);
    } else {
        /* unknown packet type, return an error */
        pmgr_error("Received unknown packet header %d @ file %s:%d",
            header, __FILE__, __LINE__
        );
        rc = -1;
    }

    return rc;
}

        
static int pmgr_connect_child(struct in_addr connect_ip, int connect_port, const char* auth)
{
    int fd = -1;
    while (fd == -1) {
        fd = pmgr_connect(connect_ip, connect_port);
        if (fd >= 0) {
            /* connected to something, check that it's who we expected to connect to */
            if (pmgr_authenticate_connect(fd, auth, auth, mpirun_authenticate_timeout) != PMGR_SUCCESS) {
                close(fd);
                fd = -1;
            }
        }
    }
    return fd;
}

/* send our rank to our parent (so it knows which child we are) and receive its rank */
static int pmgr_wireup_connect_parent_exchange(int fd, pmgr_tree_t* t)
{
    /* read rank of other end */
    int connected_rank;
    if (pmgr_read_collective(t, fd, &connected_rank, sizeof(int)) < 0) {
        /* failed to read rank */
        pmgr_error("%s failed to read rank of parent %s @ file %s:%d",
            t->name, t->parent_name, __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* write our rank to let other end who we are */
    pmgr_write_collective(t, fd, &(t->rank), sizeof(int));

    /* check that we connected to the right rank */
    if (connected_rank != t->parent_rank) {
        /* connected to the wrong rank */
        pmgr_error("%s rank of parent %d does not match expected rank %d from %s @ file %s:%d",
            t->name, connected_rank, t->parent_rank, t->parent_name, __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* read length of parent's hostname */
    int remote_host_len;
    if (pmgr_read_collective(t, fd, &remote_host_len, sizeof(int)) < 0) {
        /* failed to read host length */
        pmgr_error("%s failed to read hostlength of parent %s @ file %s:%d",
            t->name, t->parent_name, __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* read parent's hostname */
    char* remote_host = NULL;
    if (remote_host_len > 0) {
        /* allocate space to receive hostname */
        remote_host = pmgr_malloc(remote_host_len, "Remote hostname");
        if (pmgr_read_collective(t, fd, remote_host, remote_host_len) < 0) {
            /* failed to read host length */
            pmgr_error("%s failed to read hostname of parent %s @ file %s:%d",
                t->name, t->parent_name, __FILE__, __LINE__
            );
            free(remote_host);
            return PMGR_FAILURE;
        }
    }

    /* send our hostname */
    int host_len = 0;
    if (t->host != NULL) {
        host_len = strlen(t->host) + 1;
    }
    pmgr_write_collective(t, fd, &host_len, sizeof(int));
    if (host_len > 0) {
        pmgr_write_collective(t, fd, t->host, host_len);
    }

    /* connection checks out, now lookup remote address info */
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    socklen_t len = sizeof(sin);
    if (getpeername(fd, (struct sockaddr *) &sin, &len) == 0) {
        /* extract remote IP and port */
        struct in_addr remote_ip = sin.sin_addr;
        short remote_port = ntohs(sin.sin_port);

        /* fill in IP and port, and return new file descriptor */
        t->parent_fd   = fd;
        t->parent_ip   = remote_ip;
        t->parent_port = remote_port;
        t->parent_host = remote_host;

        /* rebuild parent name using remote ip and port */
        pmgr_tree_build_parent_name_ip(t);
    } else {
        pmgr_error("Extracting remote IP and port (getpeername() %m errno=%d) @ file %s:%d",
            errno, __FILE__, __LINE__
        );
        pmgr_free(remote_host);
        return PMGR_FAILURE;
    }

    /* we don't free remote host here, because we assigned it over to t->parent_host, which will be freed later */
    /*pmgr_free(remote_host)*/

    return PMGR_SUCCESS;
}

/* issue a connect to a child, and verify that we really connected to who we should */
static int pmgr_wireup_connect_parent_direct(struct in_addr connect_ip, int connect_port, pmgr_tree_t* t, const char* auth)
{
    /* attempt to connect to specified IP and port */
    int fd = -1;
    while (fd == -1) {
        fd = pmgr_connect(connect_ip, connect_port);
        if (fd >= 0) {
            /* connected to something, check that it's who we expected to connect to */
            if (pmgr_authenticate_connect(fd, auth, auth, mpirun_authenticate_timeout) == PMGR_SUCCESS) {
                /* we've authenticated our connection, now exchange ranks with our parent process,
                 * so it knows which child we are */
                if (pmgr_wireup_connect_parent_exchange(fd, t) != PMGR_SUCCESS) {
                    pmgr_error("%s failed to exchcange ranks with parent %s @ file %s:%d",
                        t->name, t->parent_name, __FILE__, __LINE__
                    );
                    close(fd);
                    fd = -1;
                    return PMGR_FAILURE;
                }
            } else {
                /* authentication failed, close this socket and try again, we connected
                 * to the right process, but perhaps we just authenticated too slowly */
                close(fd);
                fd = -1;
            }
        } else {
            /* error from connect */
        }
    }
    return PMGR_SUCCESS;
}

/* accept connections from all of our children */
static int pmgr_wireup_accept_children(int listenfd, pmgr_tree_t* t, const char* auth)
{
    /* determine how many children will be connecting */
    int count = t->num_child;
    while (count > 0) {
        /* accept a connection */
        int fd;
        struct in_addr remote_ip;
        short remote_port;
        if (pmgr_accept(listenfd, auth, &fd, &remote_ip, &remote_port) == PMGR_SUCCESS) {
            /* write our rank to let child know who it connected to */
            pmgr_write_collective(t, fd, &(t->rank), sizeof(int));

            /* send our hostname */
            int host_len = 0;
            if (t->host != NULL) {
                host_len = strlen(t->host) + 1;
            }
            pmgr_write_collective(t, fd, &host_len, sizeof(int));
            if (host_len > 0) {
                pmgr_write_collective(t, fd, t->host, host_len);
            }

            /* read rank of child */
            int rank;
            if (pmgr_read_collective(t, fd, &rank, sizeof(int)) < 0) {
                /* failed to read rank */
                close(fd);
                return PMGR_FAILURE;
            }

            /* read length of child's hostname */
            int remote_host_len;
            if (pmgr_read_collective(t, fd, &remote_host_len, sizeof(int)) < 0) {
                /* failed to read host length */
                pmgr_error("%s failed to read hostlength of parent %s @ file %s:%d",
                    t->name, t->parent_name, __FILE__, __LINE__
                );
                return PMGR_FAILURE;
            }

            /* read child's hostname */
            char* remote_host = NULL;
            if (remote_host_len > 0) {
                /* allocate space to receive hostname */
                remote_host = pmgr_malloc(remote_host_len, "Remote hostname");
                if (pmgr_read_collective(t, fd, remote_host, remote_host_len) < 0) {
                    /* failed to read host length */
                    pmgr_error("%s failed to read hostname of parent %s @ file %s:%d",
                        t->name, t->parent_name, __FILE__, __LINE__
                    );
                    free(remote_host);
                    return PMGR_FAILURE;
                }
            }

            /* scan our array to determine which child we accepted */
            int i;
            int index = -1;
            for (i = 0; i < t->num_child; i++) {
                if (t->child_rank[i] == rank) {
                    index = i;
                    break;
                }
            }

            /* record values and decrement our count if it was a child we were expecting */
            if (index != -1) {
                /* set socket, IP, and port for this child */
                t->child_fd[index]   = fd;
                t->child_ip[index]   = remote_ip;
                t->child_port[index] = remote_port;
                t->child_host[index] = remote_host;

                /* rebuild name of child using new IP and port */
                pmgr_tree_build_child_name_ip(t, index);

                /* decrement our count by one */
                count--;
            } else {
                /* unexpected child connected to us */
                close(fd);
                pmgr_free(remote_host);
                return PMGR_FAILURE;
            }
        } else {
            /* accept failed, try again */
        }
    }
    return PMGR_SUCCESS;
}

/*
 * =============================
 * Initialize and free tree data structures
 * =============================
 */

/* initialize tree to null tree */
int pmgr_tree_init_null(pmgr_tree_t* t)
{
    if (t == NULL) {
        return PMGR_FAILURE;
    }

    t->type           = PMGR_GROUP_TREE_NULL;
    t->ranks          =  0;
    t->rank           = -1;
    t->host           = NULL;
    t->name           = NULL;
    t->is_open        =  0;
    t->parent_rank    = -1;
    t->parent_host    = NULL;
    t->depth          = -1;
    t->parent_fd      = -1;
    memset(&(t->parent_ip), 0, sizeof(t->parent_ip));
    t->parent_port    = -1;
    t->parent_name    = NULL;
    t->num_child      = -1;
    t->num_child_incl = -1;
    t->child_rank = NULL;
    t->child_host = NULL;
    t->child_fd   = NULL;
    t->child_incl = NULL;
    t->child_ip   = NULL;
    t->child_port = NULL;
    t->child_name = NULL;

    return PMGR_SUCCESS;
}

/* given number of ranks and our rank with the group, create a binomail tree
 * fills in our position within the tree and allocates memory to hold socket info */
int pmgr_tree_init_binomial(pmgr_tree_t* t, int ranks, int rank)
{
    int i;

    if (t == NULL) {
        return PMGR_FAILURE;
    }

    pmgr_tree_init_null(t);

    /* compute the maximum number of children this task may have */
    int n = 1;
    int max_children = 0;
    while (n < ranks) {
        n <<= 1;
        max_children++;
    }

    /* prepare data structures to store our parent and children */
    t->type  = PMGR_GROUP_TREE_BINOMIAL;
    t->ranks = ranks;
    t->rank  = rank;
    t->depth          =  0;
    t->parent_rank    = -1;
    t->num_child      =  0;
    t->num_child_incl =  0;
    if (max_children > 0) {
        t->child_rank = (int*)   pmgr_malloc(max_children * sizeof(int),   "Child rank array");
        t->child_host = (char**) pmgr_malloc(max_children * sizeof(char*), "Child hostname array");
        t->child_fd   = (int*)   pmgr_malloc(max_children * sizeof(int),   "Child socket fd array");
        t->child_incl = (int*)   pmgr_malloc(max_children * sizeof(int),   "Child children count array");
        t->child_ip   = (struct in_addr*) pmgr_malloc(max_children * sizeof(struct in_addr), "Child IP array");
        t->child_port = (short*) pmgr_malloc(max_children * sizeof(short), "Child port array");
        t->child_name = (char**) pmgr_malloc(max_children * sizeof(char*), "Child name array");
    }

    /* set our hostname */
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        t->host = strdup(hostname);
    } else {
        pmgr_error("Getting hostname @ %s:%d", __FILE__, __LINE__);
    }

    /* set our name for printing error messages */
    pmgr_tree_build_name_rank(t->rank, t->ranks, &(t->name));

    /* initialize parent and child socket file descriptors to -1 */
    t->parent_fd = -1;
    for(i = 0; i < max_children; i++) {
        t->child_fd[i] = -1;
        t->child_host[i] = NULL;
        t->child_name[i] = NULL;
    }

    /* find our parent rank and the ranks of our children */
    int depth = 1;
    int low  = 0;
    int high = ranks - 1;
    while (high - low > 0) {
        int mid = (high - low) / 2 + (high - low) % 2 + low;
        if (low == rank) {
            t->child_rank[t->num_child] = mid;
            t->child_incl[t->num_child] = high - mid + 1;

            /* now that ranks and rank is set, we can build a name for this child */
            pmgr_tree_build_name_child_rank(t, t->num_child);

            t->num_child++;
            t->num_child_incl += (high - mid + 1);
        }
        if (mid == rank) {
            t->depth = depth;
            t->parent_rank = low;

            /* now that ranks and rank is set, we can build a name for this parent */
            pmgr_tree_build_name_parent_rank(t);
        }
        if (mid <= rank) {
            low  = mid;
        } else {
            high = mid-1;
            depth++;
        }
    }

    return PMGR_SUCCESS;
}

/* given number of ranks and our rank with the group, create a binary tree
 * fills in our position within the tree and allocates memory to hold socket info */
int pmgr_tree_init_binary(pmgr_tree_t* t, int ranks, int rank)
{
    int i;

    if (t == NULL) {
        return PMGR_FAILURE;
    }

    pmgr_tree_init_null(t);

    /* compute the maximum number of children this task may have */
    int max_children = 2;

    /* prepare data structures to store our parent and children */
    t->type  = PMGR_GROUP_TREE_BINOMIAL;
    t->ranks = ranks;
    t->rank  = rank;
    t->depth          =  0;
    t->parent_rank    = -1;
    t->num_child      =  0;
    t->num_child_incl =  0;
    if (max_children > 0) {
        t->child_rank = (int*)   pmgr_malloc(max_children * sizeof(int),   "Child rank array");
        t->child_host = (char**) pmgr_malloc(max_children * sizeof(char*), "Child hostname array");
        t->child_fd   = (int*)   pmgr_malloc(max_children * sizeof(int),   "Child socket fd array");
        t->child_incl = (int*)   pmgr_malloc(max_children * sizeof(int),   "Child children count array");
        t->child_ip   = (struct in_addr*) pmgr_malloc(max_children * sizeof(struct in_addr), "Child IP array");
        t->child_port = (short*) pmgr_malloc(max_children * sizeof(short), "Child port array");
        t->child_name = (char**) pmgr_malloc(max_children * sizeof(char*), "Child name array");
    }

    /* set our hostname */
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        t->host = strdup(hostname);
    } else {
        pmgr_error("Getting hostname @ %s:%d", __FILE__, __LINE__);
    }

    /* set our name for printing error messages */
    pmgr_tree_build_name_rank(t->rank, t->ranks, &(t->name));

    /* initialize parent and child socket file descriptors to -1 */
    t->parent_fd = -1;
    for(i = 0; i < max_children; i++) {
        t->child_fd[i] = -1;
        t->child_host[i] = NULL;
        t->child_name[i] = NULL;
    }

    /* find our parent rank and the ranks of our children */
    int low  = 0;
    int high = ranks - 1;
    while (high - low > 0) {
        /* pick the midpoint of the remaining nodes, round up if not divisible by 2 */
        int mid = (high - low) / 2 + (high - low) % 2 + low;

        /* if we are the parent for this section, set our children */
        if (low == rank) {
            /* take the rank that is furthest away as the first child */
            t->child_rank[t->num_child] = mid;
            t->child_incl[t->num_child] = high - mid + 1;

            /* now that ranks and rank is set, we can build a name for this child */
            pmgr_tree_build_name_child_rank(t, t->num_child);

            t->num_child++;
            t->num_child_incl += (high - mid + 1);

            /* if there is another rank between us and the midpoint,
             * set the next highest rank as our second child */
            low++;
            if (mid > low) {
                t->child_rank[t->num_child] = low;
                t->child_incl[t->num_child] = mid - low;

                /* now that ranks and rank is set, we can build a name for this child */
                pmgr_tree_build_name_child_rank(t, t->num_child);

                t->num_child++;
                t->num_child_incl += (mid - low);
            }

            break;
        }

        /* increase our depth from the root by one */
        t->depth++;

        /* determine whether we're in the first or second half,
         * if our rank is the midpoint or the next highest from the current low
         * then we'll be a parent in the next step, so the current low is our parent */
        if (mid <= rank) {
            if (mid == rank) {
                /* set the parent rank and its name */
                t->parent_rank = low;
                pmgr_tree_build_name_parent_rank(t);
            }
            low  = mid;
        } else {
            if (low+1 == rank) {
                /* set the parent rank and its name */
                t->parent_rank = low;
                pmgr_tree_build_name_parent_rank(t);
            }
            low  = low + 1;
            high = mid - 1;
        }
    }

    return PMGR_SUCCESS;
}

/* free all memory allocated in tree */
int pmgr_tree_free(pmgr_tree_t* t)
{
    if (t == NULL) {
        return PMGR_FAILURE;
    }

    int i;
    for (i = 0; i < t->num_child; i++) {
        if (t->child_name[i] != NULL) {
            free(t->child_host[i]);
            free(t->child_name[i]);
        }
    }

    pmgr_free(t->host);
    pmgr_free(t->name);
    pmgr_free(t->parent_name);
    pmgr_free(t->parent_host);
    pmgr_free(t->child_rank);
    pmgr_free(t->child_host);
    pmgr_free(t->child_fd);
    pmgr_free(t->child_incl);
    pmgr_free(t->child_ip);
    pmgr_free(t->child_port);
    pmgr_free(t->child_name);

    pmgr_tree_init_null(t);
  
    return PMGR_SUCCESS;
}

/* 
 * =============================
 * Functions to open/close/gather/bcast the TCP/socket tree.
 * =============================
*/

int pmgr_tree_is_open(pmgr_tree_t* t)
{
    return t->is_open;
}

/*
 * close down socket connections for tree (parent and any children),
 * free memory for tree data structures
 */
int pmgr_tree_close(pmgr_tree_t* t)
{
    /* mark the tree as being closed */
    t->is_open = 0;

    /* close socket connection with parent */
    if (t->parent_fd >= 0) {
        pmgr_shutdown(t->parent_fd);
        close(t->parent_fd);
        t->parent_fd = -1;
    }

    /* close sockets to children */
    int i;
    for(i = 0; i < t->num_child; i++) {
        if (t->child_fd[i] >= 0) {
            pmgr_shutdown(t->child_fd[i]);
            close(t->child_fd[i]);
            t->child_fd[i] = -1;
        }
    }

    /* TODO: really want to free the tree here? */
    /* free data structures */
    pmgr_tree_free(t);

    return PMGR_SUCCESS;
}

/*
 * send abort message across links, then close down tree
 */
int pmgr_tree_abort(pmgr_tree_t* t)
{
    /* send abort message to parent */
    if (t->parent_fd >= 0) {
        pmgr_write_abort(t->parent_fd);
    }

    /* send abort message to each child */
    int i;
    for(i = 0; i < t->num_child; i++) {
        if (t->child_fd[i] >= 0) {
            pmgr_write_abort(t->child_fd[i]);
        }
    }

    /* shut down our connections */
    pmgr_tree_close(t);

    return PMGR_SUCCESS;
}

/* check whether all tasks report success, exit if someone failed */
int pmgr_tree_check(pmgr_tree_t* t, int value)
{
    /* assume that everyone succeeded */
    int all_value = 1;

    /* read value from each child */
    int i;
    for (i = 0; i < t->num_child; i++) {
        if (t->child_fd[i] >= 0) {
            int child_value;
            if (pmgr_read_collective(t, t->child_fd[i], &child_value, sizeof(int)) < 0) {
                /* failed to read value from child, assume child failed */
                pmgr_error("%s reading result from child %s @ file %s:%d",
                    t->name, t->child_name[i], __FILE__, __LINE__
                );
                pmgr_tree_abort(t);
                pmgr_abort_trees();
                exit(1);
            } else {
                if (!child_value) {
                    /* child failed */
                    all_value = 0;
                }
            }
        } else {
            /* never connected to this child, assume child failed */
            all_value = 0;
        }
    }

    /* now consider my value */
    if (!value) {
        all_value = 0;
    }

    /* send result to parent */
    if (t->parent_fd >= 0) {
        /* send result to parent */
        if (pmgr_write_collective(t, t->parent_fd, &all_value, sizeof(int)) < 0) {
            pmgr_error("%s writing check tree result to parent %s @ file %s:%d",
                t->name, t->parent_name, __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
    }

    /* read result from parent */
    if (t->parent_fd >= 0) {
        if (pmgr_read_collective(t, t->parent_fd, &all_value, sizeof(int)) < 0) {
            pmgr_error("%s reading check tree result from parent %s @ file %s:%d",
                t->name, t->parent_name, __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
    }

    /* broadcast result to children */
    for (i = 0; i < t->num_child; i++) {
        if (t->child_fd[i] >= 0) {
            if (pmgr_write_collective(t, t->child_fd[i], &all_value, sizeof(int)) < 0) {
                pmgr_error("%s writing result to child %s @ file %s:%d",
                    t->name, t->child_name[i], __FILE__, __LINE__
                );
                pmgr_tree_abort(t);
                pmgr_abort_trees();
                exit(1);
            }
        }
    }

    /* if someone failed, exit */
    if (!all_value) {
        /* abort the tree */
        pmgr_tree_abort(t);
        pmgr_abort_trees();

        /* close down sockets and send close op code to srun */
        pmgr_close();

        /* exit with success if this process succeeded, exit with failure otherwise */
        if (value) {
            exit(0);
        }
        exit(1);
    }
    return PMGR_SUCCESS;
}

/* given a table containing "ranks" number of ip:port entries, open a tree */
int pmgr_tree_open_table(pmgr_tree_t* t, int ranks, int rank, const void* table, int sockfd, const char* auth)
{
    int i;

    /* compute the size of each entry in the table ip:port */
    size_t addr_size = sizeof(struct in_addr) + sizeof(short);

    /* compute our depth, parent,and children */
//    pmgr_tree_init_binomial(t, ranks, rank);
    pmgr_tree_init_binary(t, ranks, rank);

    /* establish connections, depending on the process's depth in the tree we either
     * accept a connection from our parent first or we try to connect to our children */
    int iter = 0;
    while (iter < 2) {
        int odd = (t->depth + iter) % 2;
        if (odd) {
            if (mpirun_connect_down) {
                /* connect to children */
                for (i=0; i < t->num_child; i++) {
                    /* get rank, IP, and port of child */
                    int connect_rank = t->child_rank[i];
                    t->child_ip[i]   = * (struct in_addr *)  ((char*)table + connect_rank*addr_size);
                    t->child_port[i] = * (short*) ((char*)table + connect_rank*addr_size + sizeof(struct in_addr));

                    /* now that we have the IP and port, include this info in the name */
                    pmgr_tree_build_child_name_ip(t, i);

                    /* connect to child */
                    t->child_fd[i] = pmgr_connect_child(t->child_ip[i], t->child_port[i], auth);
                    if (t->child_fd[i] < 0) {
                        /* failed to connect to child */
                        pmgr_error("%s connecting to child %s @ file %s:%d",
                            t->name, t->child_name[i], __FILE__, __LINE__
                        );
                        pmgr_tree_abort(t);
                        pmgr_abort_trees();
                        exit(1);
                    }
                }
            } else {
                /* connect to parent */
                if (t->rank != 0) {
                    /* get rank, IP, and port of parent */
                    int connect_rank  = t->parent_rank;
                    t->parent_ip   = * (struct in_addr *)  ((char*)table + connect_rank*addr_size);
                    t->parent_port = * (short*) ((char*)table + connect_rank*addr_size + sizeof(struct in_addr));

                    /* now that we have the IP and port, include this info in the name */
                    pmgr_tree_build_parent_name_ip(t);
                    
                    /* connect to parent */
                    if (pmgr_wireup_connect_parent_direct(t->parent_ip, t->parent_port, t, auth) != PMGR_SUCCESS) {
                        /* failed to connect to child */
                        pmgr_error("%s connecting to parent %s @ file %s:%d",
                            t->name, t->parent_name, __FILE__, __LINE__
                        );
                        pmgr_tree_abort(t);
                        pmgr_abort_trees();
                        exit(1);
                    }
                }
            }
        } else {
            if (mpirun_connect_down) {
                /* accept a connection from parent */
                if (t->rank != 0) {
                    if (pmgr_accept(sockfd, auth, &(t->parent_fd), &(t->parent_ip), &(t->parent_port)) != PMGR_SUCCESS) {
                        pmgr_error("%s failed to accept parent connection %s (%m errno=%d) @ file %s:%d",
                            t->name, t->parent_name, errno, __FILE__, __LINE__
                        );
                        pmgr_tree_abort(t);
                        pmgr_abort_trees();
                        exit(1);
                    }

                    /* if we made it this far, we established a connection, rebuild name to include IP and port */
                    pmgr_tree_build_parent_name_ip(t);
                }
            } else {
                /* accept connections from children */
                if (pmgr_wireup_accept_children(sockfd, t, auth) != PMGR_SUCCESS) {
                    pmgr_error("%s failed to accept children @ file %s:%d",
                        t->name, __FILE__, __LINE__
                    );
                    pmgr_tree_abort(t);
                    pmgr_abort_trees();
                    exit(1);
                }
            }
        }
        iter++;
    }

    /* mark the tree as being open */
    t->is_open = 1;

    /* check whether everyone succeeded in connecting */
    pmgr_tree_check(t, 1);

    return PMGR_SUCCESS;
}

static int pmgr_tree_open_nodelist_scan_connect_children(
    pmgr_tree_t* t, const char* nodelist, int nodes, const char* portrange, int portoffset,
    const char* auth)
{
    /* we connect to our children in reverse order, which is in increasing rank order,
     * which seems to provide better performance on systems running SLURM */
    int i;
    //for (i = 0; i < t->num_child; i++)
    for (i = t->num_child - 1; i >= 0; i--) {
        /* get the rank of the child we'll connect to */
        int rank = t->child_rank[i];
        if (rank >= nodes) {
            /* child rank is out of range */
            pmgr_error("Child rank %d is out of range of %d nodes %s @ file %s:%d",
                rank, nodes, nodelist, __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }

        /* get hostname of child (note we need to add one to the rank) */
        char hostname[1024];
        if (pmgr_range_nodelist_nth(nodelist, rank+1, hostname, sizeof(hostname))
            != PMGR_SUCCESS)
        {
            /* failed to extract child hostname from nodelist */
            pmgr_error("Failed to extract hostname for node %d from %s @ file %s:%d",
                rank+1, nodelist, __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }

        /* attempt to connect to child on this hostname using given portrange */
        int fd;
        struct in_addr ip;
        short port;
        if (pmgr_connect_hostname(rank, hostname, portrange, portoffset, auth, auth, &fd, &ip, &port) == PMGR_SUCCESS) {
            /* connected to child, record ip, port, and socket */
            t->child_fd[i]   = fd;
            t->child_ip[i]   = ip;
            t->child_port[i] = port;

            /* rebuild name using new ip and port */
            pmgr_tree_build_child_name_ip(t, i);
        } else {
            /* failed to connect to child */
            pmgr_error("%s connecting to child %s on %s @ file %s:%d",
                t->name, t->child_name[i], hostname, __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
    }

    return PMGR_SUCCESS;
}

static int pmgr_wireup_connect_parent_scan(
    pmgr_tree_t* t, const char* nodelist, int nodes, const char* portrange, int portoffset,
    const char* auth)
{
    /* get hostname of parent (note we need to add one to the rank) */
    char hostname[1024];
    int rank = t->parent_rank;
    if (pmgr_range_nodelist_nth(nodelist, rank+1, hostname, sizeof(hostname))
        != PMGR_SUCCESS)
    {
        /* failed to extract child hostname from nodelist */
        pmgr_error("Failed to extract hostname for node %d from %s @ file %s:%d",
            rank+1, nodelist, __FILE__, __LINE__
        );
        pmgr_tree_abort(t);
        pmgr_abort_trees();
        exit(1);
    }

    /* attempt to connect to parent on this hostname using given portrange */
    int fd;
    struct in_addr ip;
    short port;
    if (pmgr_connect_hostname(rank, hostname, portrange, portoffset, auth, auth, &fd, &ip, &port) == PMGR_SUCCESS) {
        /* we've authenticated our connection, now exchange ranks with our parent process,
         * so it knows who we are */
        if (pmgr_wireup_connect_parent_exchange(fd, t) != PMGR_SUCCESS) {
            pmgr_error("%s failed to exchcange ranks with parent %s @ file %s:%d",
                t->name, t->parent_name, __FILE__, __LINE__
            );
            close(fd);
            fd = -1;
            return PMGR_FAILURE;
        }
    } else {
        /* failed to connect to parent */
        pmgr_error("%s connecting to parent %s on %s failed @ file %s:%d",
            t->name, t->parent_name, hostname, __FILE__, __LINE__
        );
        pmgr_tree_abort(t);
        pmgr_abort_trees();
        exit(1);
    }

    return PMGR_SUCCESS;
}

/* given a table of ranks number of ip:port entries, open a tree */
int pmgr_tree_open_nodelist_scan(pmgr_tree_t* t, const char* nodelist, const char* portrange, int portoffset, int sockfd, const char* auth)
{
    /* determine number of nodes in nodelist */
    int nodes;
    pmgr_range_nodelist_size(nodelist, &nodes);

    /* establish connections, depending on the process's depth in the tree we either
     * accept a connection from our parent first or we try to connect to our children */
    int iter = 0;
    while (iter < 2) {
        int odd = (t->depth + iter) % 2;
        if (odd) {
            if (mpirun_connect_down) {
                /* connect to children */
                if (pmgr_tree_open_nodelist_scan_connect_children(t, nodelist, nodes,
                    portrange, portoffset, auth) != PMGR_SUCCESS)
                {
                    pmgr_error("%s failed to connect to children @ file %s:%d",
                        t->name, __FILE__, __LINE__
                    );
                    pmgr_tree_abort(t);
                    pmgr_abort_trees();
                    exit(1);
                }
           } else {
                /* connect to parent */
                if (t->rank != 0) {
                    if (pmgr_wireup_connect_parent_scan(t, nodelist, nodes,
                        portrange, portoffset, auth) != PMGR_SUCCESS)
                    {
                        pmgr_error("%s failed to connect to parent %s @ file %s:%d",
                            t->name, t->parent_name, __FILE__, __LINE__
                        );
                        pmgr_tree_abort(t);
                        pmgr_abort_trees();
                        exit(1);
                    }
                }
           }
        } else {
            if (mpirun_connect_down) {
                /* accept a connection from parent (so long as we're not rank 0) */
                if (t->rank != 0) {
                    if (pmgr_accept(sockfd, auth, &(t->parent_fd), &(t->parent_ip), &(t->parent_port)) != PMGR_SUCCESS) {
                        pmgr_error("%s failed to accept parent connection %s (%m errno=%d) @ file %s:%d",
                            t->name, t->parent_name, errno, __FILE__, __LINE__
                        );
                        pmgr_tree_abort(t);
                        pmgr_abort_trees();
                        exit(1);
                    }

                    /* if we make it this far, rebuild the parent name with new IP and port */
                    pmgr_tree_build_parent_name_ip(t);
                }
            } else {
                /* accept connections from children */
                if (pmgr_wireup_accept_children(sockfd, t, auth) != PMGR_SUCCESS) {
                    pmgr_error("%s failed to accept children @ file %s:%d",
                        t->name, __FILE__, __LINE__
                    );
                    pmgr_tree_abort(t);
                    pmgr_abort_trees();
                    exit(1);
                }
            }
        }
        iter++;
    }

    /* mark the tree as being open */
    t->is_open = 1;

    /* check whether everyone succeeded in connecting */
    pmgr_tree_check(t, 1);

    return PMGR_SUCCESS;
}

/* 
 * Dong Ahn: open socket tree across MPI tasks 
 * using CMB primitives. This is a copy of
 * pmgr_tree_open_pmi replacint its PMI calls
 * with CMB calls.
 */
int pmgr_tree_open_cmb (pmgr_tree_t *t, x_comm_fab_cxt *cf_cxt,
		       int ranks, int rank, const char *auth)
{
#ifdef HAVE_FLUX_CMB
#define FLUX_CMB_MAX_STR 128

    int i, error_cnt, put_cnt;
    char keystr[FLUX_CMB_MAX_STR];
    char valstr[FLUX_CMB_MAX_STR];

    cmb_t cmb_cxt = (cmb_t) cf_cxt->cxt;

    /* create a socket to accept connections */
    int sockfd = -1;
    struct in_addr ip;
    short port;
    if (pmgr_open_listening_socket(NULL, 0, &sockfd, 
                                   &ip, &port) != PMGR_SUCCESS) {
        pmgr_error("Creating listening socket @ file %s:%d",
            __FILE__, __LINE__
        );
    }

    /* insert our IP address, keyed by our rank */
    if (snprintf(keystr, FLUX_CMB_MAX_STR, 
                 "%d", rank) >= FLUX_CMB_MAX_STR) {
        pmgr_error("Could not copy rank into key buffer @ file %s:%d",
            __FILE__, __LINE__
        );
    }

    if (snprintf(valstr, FLUX_CMB_MAX_STR, 
                 "%s:%hd", inet_ntoa(ip), port) >= FLUX_CMB_MAX_STR) {
        pmgr_error("Could not copy ip:port into value buffer @ file %s:%d",
            __FILE__, __LINE__
        );
    }
    
    if (cmb_kvs_put (cmb_cxt, keystr, valstr) < 0) {
        pmgr_error("cmb_kvs_put could not put copy"
            " key/value pair into kvs @ file %s:%d",
            __FILE__, __LINE__
        );
    }

    if (cmb_kvs_commit (cmb_cxt, &error_cnt, &put_cnt) < 0) {
        pmgr_error("cmb_kvs_put could not commit "
            "error_cnt(%d), put_cnt(%d) @ file %s:%d",
            error_cnt, put_cnt, __FILE__, __LINE__
        );
    }

    if (cmb_barrier (cmb_cxt, "topen-cmb", ranks) < 0) {
        pmgr_error("cmb_barrier failed @ file %s:%d",
            __FILE__, __LINE__
        );
    }    
    
    /* compute our depth, parent,and children */
    pmgr_tree_init_binary(t, ranks, rank);

    /* establish connections, depending on the process's 
     * depth in the tree we either
     * accept a connection from our parent first 
     * or we try to connect to our children 
     * */
    int iter = 0;
    char *res_val = NULL;
    char *ipstr   = NULL;
    char *portstr = NULL;
    while (iter < 2) {
        int odd = (t->depth + iter) % 2;
        if (odd) {
            if (mpirun_connect_down) {
                /* connect to children */
                for (i=0; i < t->num_child; i++) {
                    /* get rank, IP, and port of child */
                    int connect_rank = t->child_rank[i];

                    /* build the key for this process */
                    if (snprintf (keystr, FLUX_CMB_MAX_STR, 
                                  "%d", connect_rank) >= 128) {
                        pmgr_error("Could not copy rank %d "
                            "into key buffer @ file %s:%d",
                            connect_rank, __FILE__, __LINE__
                        );
                    }

                    /* extract value for this process 
                     * res_val is returned by strdup and it isn't
                     * always safe to free it                  
                     */
		    res_val = cmb_kvs_get (cmb_cxt, 
                                  (const char *) keystr);
                    if ( res_val < 0 ) {
                        pmgr_error("Could not get key/value for %s"
                            " @ file %s:%d",
                            keystr, __FILE__, __LINE__
                        );
                    }

                    /* break the ip:port string */
                    ipstr = strtok(res_val, ":");
                    portstr = strtok(NULL, ":");

                    /* convert ip and port to proper datatypes */
                    if (inet_aton(ipstr, &(t->child_ip[i])) == 0) {
                        pmgr_error("Failed to convert dotted"
                                   " decimal notation to struct" 
                                   " in_addr for %s @ file %s:%d",
                            ipstr, __FILE__, __LINE__
                        );
                    }
                    t->child_port[i] = atoi(portstr);

                    /* now that we have the IP and port, 
                     * include this info in the name 
                     */
                    pmgr_tree_build_child_name_ip(t, i);

                    /* connect to child */
                    t->child_fd[i] = pmgr_connect_child(t->child_ip[i], 
                                       t->child_port[i], auth);
                    if (t->child_fd[i] < 0) {
                        /* failed to connect to child */
                        pmgr_error("%s connecting to child %s @ file %s:%d",
                            t->name, t->child_name[i], __FILE__, __LINE__
                        );
                        pmgr_tree_abort(t);
                        pmgr_abort_trees();
                        exit(1);
                    }
                }
            } else {
                /* connect to parent */
                char *res_val = NULL;
                if (t->rank != 0) {
                    /* get rank, IP, and port of parent */
                    int connect_rank = t->parent_rank;

                    /* build the key for this process */
                    if (snprintf(keystr, FLUX_CMB_MAX_STR,
                                 "%d", connect_rank) >= 128) {
                        pmgr_error("Could not copy rank %d into"
                            " key buffer @ file %s:%d",
                            connect_rank, __FILE__, __LINE__
                        );
                    }

                    /* extract value for this process */
		    res_val = cmb_kvs_get (cmb_cxt, 
                                  (const char *) keystr );
                    if ( res_val < 0) {
                        pmgr_error("Could not get key/value"
                            " for %s @ file %s:%d",
                            keystr, __FILE__, __LINE__
                        );
                    }

                    /* break the ip:port string */
                    ipstr = strtok(res_val, ":");
                    portstr = strtok(NULL, ":");

                    /* convert ip and port to proper datatypes */
                    if (inet_aton(ipstr, &(t->parent_ip)) == 0) {
                        pmgr_error("Failed to convert dotted"
                                   "decimal notation to struct in_addr"
                                   "for %s @ file %s:%d",
                            ipstr, __FILE__, __LINE__
                        );
                    }
                    t->parent_port = atoi(portstr);

                    /* now that we have the IP and port, include 
                     * this info in the name 
                     */
                    pmgr_tree_build_parent_name_ip(t);
                    
                    /* connect to parent */
                    if (pmgr_wireup_connect_parent_direct(t->parent_ip, 
                                                          t->parent_port, 
                                                          t, auth) != PMGR_SUCCESS) {
                        /* failed to connect to child */
                        pmgr_error("%s connecting to parent %s @ file %s:%d",
                            t->name, t->parent_name, __FILE__, __LINE__
                        );
                        pmgr_tree_abort(t);
                        pmgr_abort_trees();
                        exit(1);
                    }
                }
            }
        } else {
            if (mpirun_connect_down) {
                /* accept a connection from parent */
                if (t->rank != 0) {
                    if (pmgr_accept(sockfd, 
                                    auth, 
                                    &(t->parent_fd), 
                                    &(t->parent_ip), 
                                    &(t->parent_port)) != PMGR_SUCCESS) {
                        pmgr_error("%s failed to accept parent"
                            " connection %s (%m errno=%d) @ file %s:%d",
                            t->name, t->parent_name, errno, __FILE__, __LINE__
                        );
                        pmgr_tree_abort(t);
                        pmgr_abort_trees();
                        exit(1);
                    }

                    /* if we made it this far, we established 
                     * a connection, rebuild name to include IP and port */
                    pmgr_tree_build_parent_name_ip(t);
                }
            } else {
                /* accept connections from children */
                if (pmgr_wireup_accept_children(sockfd, 
                                                t, auth) != PMGR_SUCCESS) {
                    pmgr_error("%s failed to accept children @ file %s:%d",
                        t->name, __FILE__, __LINE__
                    );
                    pmgr_tree_abort(t);
                    pmgr_abort_trees();
                    exit(1);
                }
            }
        }
        iter++;
    }

    /* mark the tree as being open */
    t->is_open = 1;

    /* check whether everyone succeeded in connecting */
    pmgr_tree_check(t, 1);

    /* close our listening socket */
    if (sockfd >= 0) {
        close(sockfd);
        sockfd = -1;
    }
#endif /* ifdef HAVE_FLUX_CMB */

    return PMGR_SUCCESS;
}

/* open socket tree across MPI tasks */
int pmgr_tree_open_pmi(pmgr_tree_t* t, int ranks, int rank, const char* auth)
{
#ifdef HAVE_PMI
    int i;

    /* create a socket to accept connections */
    int sockfd = -1;
    struct in_addr ip;
    short port;
    if (pmgr_open_listening_socket(NULL, 0, &sockfd, &ip, &port) != PMGR_SUCCESS) {
        pmgr_error("Creating listening socket @ file %s:%d",
            __FILE__, __LINE__
        );
        PMI_Abort(1, "Failed to create listening socket");
    }

    /* get the number of bytes we need for our KVS name */
    int kvslen = 0;
    if (PMI_KVS_Get_name_length_max(&kvslen) != PMI_SUCCESS) {
        pmgr_error("Getting maximum length for PMI KVS name @ file %s:%d",
            __FILE__, __LINE__
        );
        PMI_Abort(1, "Failed to get maximum length for PMI KVS space name");
    }

    /* get the maximum number of bytes allowed for a KVS key */
    int keylen = 0;
    if (PMI_KVS_Get_key_length_max(&keylen) != PMI_SUCCESS) {
        pmgr_error("Getting maximum length for PMI key length @ file %s:%d",
            __FILE__, __LINE__
        );
        PMI_Abort(1, "Failed to get maximum length for PMI key length");
    }

    /* get the maximum number of bytes allowed for a KVS value */
    int vallen = 0;
    if (PMI_KVS_Get_value_length_max(&vallen) != PMI_SUCCESS) {
        pmgr_error("Getting maximum length for PMI value length @ file %s:%d",
            __FILE__, __LINE__
        );
        PMI_Abort(1, "Failed to get maximum length for PMI value length");
    }

    /* allocate space to hold kvs name, key, and value */
    char* kvsstr = (char*) pmgr_malloc(kvslen, "KVS name buffer");
    char* keystr = (char*) pmgr_malloc(keylen, "KVS key buffer");
    char* valstr = (char*) pmgr_malloc(vallen, "KVS value buffer");

    /* lookup our KVS name */
    if (PMI_KVS_Get_my_name(kvsstr, kvslen) != PMI_SUCCESS) {
        pmgr_error("Could not copy KVS name into buffer @ file %s:%d",
            __FILE__, __LINE__
        );
        PMI_Abort(1, "Could not copy KVS name into buffer");
    }

    /* insert our IP address, keyed by our rank */
    if (snprintf(keystr, keylen, "%d", rank) >= keylen) {
        pmgr_error("Could not copy rank into key buffer @ file %s:%d",
            __FILE__, __LINE__
        );
        PMI_Abort(1, "Could not copy rank into key buffer");
    }
    if (snprintf(valstr, vallen, "%s:%hd", inet_ntoa(ip), port) >= vallen) {
        pmgr_error("Could not copy ip:port into value buffer @ file %s:%d",
            __FILE__, __LINE__
        );
        PMI_Abort(1, "Could not copy ip:port into value buffer");
    }
    if (PMI_KVS_Put(kvsstr, keystr, valstr) != PMI_SUCCESS) {
        pmgr_error("Failed to put IP address in PMI %s/%s @ file %s:%d",
            keystr, valstr, __FILE__, __LINE__
        );
        PMI_Abort(1, "Failed to put IP address in PMI");
    }

    /* commit our ip:port value and issue a barrier */
    if (PMI_KVS_Commit(kvsstr) != PMI_SUCCESS) {
        pmgr_error("Failed to commit IP KVS in PMI @ file %s:%d",
            __FILE__, __LINE__
        );
        PMI_Abort(1, "Failed to commit IP address in PMI");
    }
    if (PMI_Barrier() != PMI_SUCCESS) {
        pmgr_error("Failed to complete barrier after commit in PMI @ file %s:%d",
            __FILE__, __LINE__
        );
        PMI_Abort(1, "Failed to complete barrier after commit in PMI");
    }

    /* compute our depth, parent,and children */
    pmgr_tree_init_binary(t, ranks, rank);

    /* establish connections, depending on the process's depth in the tree we either
     * accept a connection from our parent first or we try to connect to our children */
    int iter = 0;
    while (iter < 2) {
        int odd = (t->depth + iter) % 2;
        if (odd) {
            if (mpirun_connect_down) {
                /* connect to children */
                for (i=0; i < t->num_child; i++) {
                    /* get rank, IP, and port of child */
                    int connect_rank = t->child_rank[i];

                    /* build the key for this process */
                    if (snprintf(keystr, keylen, "%d", connect_rank) >= keylen) {
                        pmgr_error("Could not copy rank %d into key buffer @ file %s:%d",
                            connect_rank, __FILE__, __LINE__
                        );
                        PMI_Abort(1, "Could not copy rank into key buffer");
                    }

                    /* extract value for this process */
                    if (PMI_KVS_Get(kvsstr, keystr, valstr, vallen) != PMI_SUCCESS) {
                        pmgr_error("Could not get key/value for %s @ file %s:%d",
                            keystr, __FILE__, __LINE__
                        );
                        PMI_Abort(1, "Could not copy rank into key buffer");
                    }

                    /* break the ip:port string */
                    char* ipstr = strtok(valstr, ":");
                    char* portstr = strtok(NULL, ":");

                    /* convert ip and port to proper datatypes */
                    if (inet_aton(ipstr, &(t->child_ip[i])) == 0) {
                        pmgr_error("Failed to convert dotted decimal notation to struct in_addr for %s @ file %s:%d",
                            ipstr, __FILE__, __LINE__
                        );
                        PMI_Abort(1, "Could not convert IP address string to struct");
                    }
                    t->child_port[i] = atoi(portstr);

                    /* now that we have the IP and port, include this info in the name */
                    pmgr_tree_build_child_name_ip(t, i);

                    /* connect to child */
                    t->child_fd[i] = pmgr_connect_child(t->child_ip[i], t->child_port[i], auth);
                    if (t->child_fd[i] < 0) {
                        /* failed to connect to child */
                        pmgr_error("%s connecting to child %s @ file %s:%d",
                            t->name, t->child_name[i], __FILE__, __LINE__
                        );
                        pmgr_tree_abort(t);
                        pmgr_abort_trees();
                        exit(1);
                    }
                }
            } else {
                /* connect to parent */
                if (t->rank != 0) {
                    /* get rank, IP, and port of parent */
                    int connect_rank = t->parent_rank;

                    /* build the key for this process */
                    if (snprintf(keystr, keylen, "%d", connect_rank) >= keylen) {
                        pmgr_error("Could not copy rank %d into key buffer @ file %s:%d",
                            connect_rank, __FILE__, __LINE__
                        );
                        PMI_Abort(1, "Could not copy rank into key buffer");
                    }

                    /* extract value for this process */
                    if (PMI_KVS_Get(kvsstr, keystr, valstr, vallen) != PMI_SUCCESS) {
                        pmgr_error("Could not get key/value for %s @ file %s:%d",
                            keystr, __FILE__, __LINE__
                        );
                        PMI_Abort(1, "Could not copy rank into key buffer");
                    }

                    /* break the ip:port string */
                    char* ipstr = strtok(valstr, ":");
                    char* portstr = strtok(NULL, ":");

                    /* convert ip and port to proper datatypes */
                    if (inet_aton(ipstr, &(t->parent_ip)) == 0) {
                        pmgr_error("Failed to convert dotted decimal notation to struct in_addr for %s @ file %s:%d",
                            ipstr, __FILE__, __LINE__
                        );
                        PMI_Abort(1, "Could not convert IP address string to struct");
                    }
                    t->parent_port = atoi(portstr);

                    /* now that we have the IP and port, include this info in the name */
                    pmgr_tree_build_parent_name_ip(t);
                    
                    /* connect to parent */
                    if (pmgr_wireup_connect_parent_direct(t->parent_ip, t->parent_port, t, auth) != PMGR_SUCCESS) {
                        /* failed to connect to child */
                        pmgr_error("%s connecting to parent %s @ file %s:%d",
                            t->name, t->parent_name, __FILE__, __LINE__
                        );
                        pmgr_tree_abort(t);
                        pmgr_abort_trees();
                        exit(1);
                    }
                }
            }
        } else {
            if (mpirun_connect_down) {
                /* accept a connection from parent */
                if (t->rank != 0) {
                    if (pmgr_accept(sockfd, auth, &(t->parent_fd), &(t->parent_ip), &(t->parent_port)) != PMGR_SUCCESS) {
                        pmgr_error("%s failed to accept parent connection %s (%m errno=%d) @ file %s:%d",
                            t->name, t->parent_name, errno, __FILE__, __LINE__
                        );
                        pmgr_tree_abort(t);
                        pmgr_abort_trees();
                        exit(1);
                    }

                    /* if we made it this far, we established a connection, rebuild name to include IP and port */
                    pmgr_tree_build_parent_name_ip(t);
                }
            } else {
                /* accept connections from children */
                if (pmgr_wireup_accept_children(sockfd, t, auth) != PMGR_SUCCESS) {
                    pmgr_error("%s failed to accept children @ file %s:%d",
                        t->name, __FILE__, __LINE__
                    );
                    pmgr_tree_abort(t);
                    pmgr_abort_trees();
                    exit(1);
                }
            }
        }
        iter++;
    }

    /* mark the tree as being open */
    t->is_open = 1;

    /* check whether everyone succeeded in connecting */
    pmgr_tree_check(t, 1);

    /* free the kvs name, key, and value */
    pmgr_free(valstr);
    pmgr_free(keystr);
    pmgr_free(kvsstr);

    /* close our listening socket */
    if (sockfd >= 0) {
        close(sockfd);
        sockfd = -1;
    }
#endif /* ifdef HAVE_PMI */

    return PMGR_SUCCESS;
}

/* open socket tree across MPI tasks */
int pmgr_tree_open_mpirun(pmgr_tree_t* t, int ranks, int rank, const char* auth)
{
    int i;

    /* initialize the tree as a binomial tree */
    pmgr_tree_init_binomial(t, ranks, rank);

    /* create a socket to accept connection from parent */
    int sockfd = -1;
    struct in_addr ip;
    short port;
    if (pmgr_open_listening_socket(NULL, 0, &sockfd, &ip, &port) != PMGR_SUCCESS) {
        pmgr_error("Creating listening socket @ file %s:%d",
            __FILE__, __LINE__
        );
        exit(1);
    }

    /* allocate buffer to receive ip:port table for all tasks */
    int sendcount = sizeof(ip) + sizeof(port);
    void* recvbuf = (void*) pmgr_malloc(sendcount * t->ranks, "Receive buffer for socket table");

    /* fill in send buffer with our ip:port */
    void* sendbuf = (void*) pmgr_malloc(sendcount, "Send buffer for socket data");
    memcpy(sendbuf, &ip, sizeof(ip));
    memcpy((char*)sendbuf + sizeof(ip), &port, sizeof(port));

    /* gather ip:port info to rank 0 via mpirun,
     * explicitly call mpirun_gather since tcp tree is not setup */
    pmgr_mpirun_gather(sendbuf, sendcount, recvbuf, 0);

    pmgr_free(sendbuf);

    /* if i'm not rank 0, accept a connection (from parent) and receive socket table */
    if (t->rank != 0) {
        if (pmgr_accept(sockfd, auth, &(t->parent_fd), &(t->parent_ip), &(t->parent_port)) == PMGR_SUCCESS) {
            /* rebuild the parent name given the IP and port */
            pmgr_tree_build_parent_name_ip(t);

            /* if we're not using PMI, we need to read the ip:port table */
            if (pmgr_read_collective(t, t->parent_fd, recvbuf, sendcount * t->ranks) < 0) {
                pmgr_error("%s receiving IP:port table from parent %s @ file %s:%d",
                    t->name, t->parent_name, __FILE__, __LINE__
                );
                pmgr_tree_abort(t);
                pmgr_abort_trees();
                exit(1);
            }
        } else {
            pmgr_error("%s failed to accept parent connection %s (%m errno=%d) @ file %s:%d",
                t->name, t->parent_name, errno, __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
    }

    /* for each child, open socket connection and forward socket table */
    for (i=0; i < t->num_child; i++) {
        /* get rank, IP, and port of child */
        int c = t->child_rank[i];
        t->child_ip[i]   = * (struct in_addr *)  ((char*)recvbuf + sendcount*c);
        t->child_port[i] = * (short*) ((char*)recvbuf + sendcount*c + sizeof(ip));

        /* rebuild name of child using IP and port */
        pmgr_tree_build_child_name_ip(t, i);

        /* connect to child */
        t->child_fd[i] = pmgr_connect_child(t->child_ip[i], t->child_port[i], auth);
        if (t->child_fd[i] >= 0) {
            /* connected to child, now forward IP table */
            if (pmgr_write_collective(t, t->child_fd[i], recvbuf, sendcount * t->ranks) < 0) {
                pmgr_error("%s writing IP:port table to child %s @ file %s:%d",
                    t->name, t->child_name[i], __FILE__, __LINE__
                );
                pmgr_tree_abort(t);
                pmgr_abort_trees();
                exit(1);
            }
        } else {
            /* failed to connect to child */
            pmgr_error("%s connecting to child %s @ file %s:%d",
                t->name, t->child_name[i], __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
    }

    /* free off the ip:port table */
    pmgr_free(recvbuf);

    /* close our listening socket */
    if (sockfd >= 0) {
        close(sockfd);
        sockfd = -1;
    }

    /* mark the tree as being open */
    t->is_open = 1;

    /* check whether everyone succeeded in connecting */
    pmgr_tree_check(t, 1);

    return PMGR_SUCCESS;
}

/* open socket tree across MPI tasks */
int pmgr_tree_open(pmgr_tree_t* t, int ranks, int rank, const char* auth)
{
    int rc = PMGR_SUCCESS;

    if (mpirun_pmi_enable) {
        /* use the process management interface */
        rc = pmgr_tree_open_pmi(t, ranks, rank, auth);
    } else if (mpirun_flux_cmb_enable) {
	rc = pmgr_tree_open_cmb(t, &comm_fab_cxt, ranks, rank, auth); 
    } else if (mpirun_shm_enable && ranks >= mpirun_shm_threshold) {
        /* use SLURM env vars and shared memory */
        rc = pmgr_tree_open_slurm(t, ranks, rank, auth);
    } else {
        /* bounce off mpirun to setup our tree */
        rc = pmgr_tree_open_mpirun(t, ranks, rank, auth);
    }

    return rc;
}

/*
 * =============================
 * Colletive implementations over tree
 * As written, these algorithms work for any tree whose children collectively
 * cover a consecutive range of ranks starting with the rank one more than the
 * parent.  Further more, the "last" child should be the nearest and the "first"
 * child should be the one furthest away from the parent.
 * =============================
 */

/* broadcast size bytes from buf on rank 0 using socket tree */
int pmgr_tree_bcast(pmgr_tree_t* t, void* buf, int size)
{
    /* if i'm not rank 0, receive data from parent */
    if (t->rank != 0) {
        if (pmgr_read_collective(t, t->parent_fd, buf, size) < 0) {
            pmgr_error("%s receiving broadcast data from parent %s @ file %s:%d",
                t->name, t->parent_name, __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
    }

    /* for each child, forward data */
    int i;
    for(i = 0; i < t->num_child; i++) {
        if (pmgr_write_collective(t, t->child_fd[i], buf, size) < 0) {
            pmgr_error("%s broadcasting data to child %s @ file %s:%d",
                t->name, t->child_name[i], __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
    }

    /* check that everyone succeeded */
    int all_success = pmgr_tree_check(t, 1);
    return all_success;
}

/* gather sendcount bytes from sendbuf on each task into recvbuf on rank 0 */
int pmgr_tree_gather(pmgr_tree_t* t, void* sendbuf, int sendcount, void* recvbuf)
{
    int bigcount = (t->num_child_incl+1) * sendcount;
    void* bigbuf = recvbuf;

    /* if i'm not rank 0, create a temporary buffer to gather child data */
    if (t->rank != 0) {
        bigbuf = (void*) pmgr_malloc(bigcount, "Temporary gather buffer in pmgr_gather_tree");
    }

    /* copy my own data into buffer */
    memcpy(bigbuf, sendbuf, sendcount);

    /* if i have any children, receive their data */
    int i;
    int offset = sendcount;
    for(i = t->num_child-1; i >= 0; i--) {
        if (pmgr_read_collective(t, t->child_fd[i], (char*)bigbuf + offset, sendcount * t->child_incl[i]) < 0) {
            pmgr_error("%s gathering data from child %s @ file %s:%d",
                t->name, t->child_name[i], __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
        offset += sendcount * t->child_incl[i];
    }

    /* if i'm not rank 0, send to parent and free temporary buffer */
    if (t->rank != 0) {
        if (pmgr_write_collective(t, t->parent_fd, bigbuf, bigcount) < 0) {
            pmgr_error("%s sending gathered data to parent %s @ file %s:%d",
                t->name, t->parent_name, __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
        pmgr_free(bigbuf);
    }

    /* check that everyone succeeded */
    int all_success = pmgr_tree_check(t, 1);
    return all_success;
}

/* scatter sendcount byte chunks from sendbuf on rank 0 to recvbuf on each task */
int pmgr_tree_scatter(pmgr_tree_t* t, void* sendbuf, int sendcount, void* recvbuf)
{
    int bigcount = (t->num_child_incl+1) * sendcount;
    void* bigbuf = sendbuf;

    /* if i'm not rank 0, create a temporary buffer to receive data from parent */
    if (t->rank != 0) {
        bigbuf = (void*) pmgr_malloc(bigcount, "Temporary scatter buffer in pmgr_scatter_tree");
        if (pmgr_read_collective(t, t->parent_fd, bigbuf, bigcount) < 0) {
            pmgr_error("%s receiving scatter data from parent %s @ file %s:%d",
                t->name, t->parent_name, __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
    }

    /* if i have any children, scatter data to them */
    int i;
    for(i = 0; i < t->num_child; i++) {
        if (pmgr_write_collective(t, t->child_fd[i], (char*)bigbuf + sendcount * (t->child_rank[i] - t->rank),
                sendcount * t->child_incl[i]) < 0)
        {
            pmgr_error("%s scattering data to child %s @ file %s:%d",
                t->name, t->child_name[i], __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
    }

    /* copy my data into my receive buffer */
    memcpy(recvbuf, bigbuf, sendcount);

    /* if i'm not rank 0, free temporary buffer */
    if (t->rank != 0) {
        pmgr_free(bigbuf);
    }

    /* check that everyone succeeded */
    int all_success = pmgr_tree_check(t, 1);
    return all_success;
}

/* computes maximum integer across all processes and saves it to recvbuf on rank 0 */
int pmgr_tree_allreduce_int64t(pmgr_tree_t* t, int64_t* sendint, int64_t* recvint, pmgr_op op)
{
    /* initialize current value using our value */
    int64_t val = *sendint;

    /* if i have any children, receive and reduce their data */
    int i;
    for(i = t->num_child-1; i >= 0; i--) {
        int64_t child_value;
        if (pmgr_read_collective(t, t->child_fd[i], &child_value, sizeof(child_value)) < 0) {
            pmgr_error("%s reducing data from child %s @ file %s:%d",
                t->name, t->child_name[i], __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        } else {
            if (op == PMGR_SUM) {
                val += child_value;
            } else if (op == PMGR_MAX && child_value > val) {
                val = child_value;
            }
        }
    }

    /* if i'm not rank 0, send to parent, otherwise copy val to recvint */
    if (t->rank != 0) {
        if (pmgr_write_collective(t, t->parent_fd, &val, sizeof(val)) < 0) {
            pmgr_error("%s sending reduced data to parent %s @ file %s:%d",
                t->name, t->parent_name, __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
    } else {
        /* this is rank 0, save val to recvint */
        *recvint = val;
    }

    /* broadcast the result back out to everyone */
    pmgr_tree_bcast(t, recvint, sizeof(int64_t));

    /* No need to do a tree check here since we do one at end of above bcast */
    /*pmgr_tree_check(t, 1);*/

    return PMGR_SUCCESS;
}

/* collects all data from all tasks into recvbuf which is at most max_recvcount bytes big,
 * effectively works like a gatherdv */
int pmgr_tree_aggregate(pmgr_tree_t* t, const void* sendbuf, int64_t sendcount, void* recvbuf, int64_t recvcount, int64_t* written)
{
    /* get total count of incoming bytes */
    int64_t total;
    int64_t portion = sendcount;
    if (pmgr_tree_allreduce_int64t(t, &portion, &total, PMGR_SUM) != PMGR_SUCCESS) {
        pmgr_error("Summing values failed @ file %s:%d",
            __FILE__, __LINE__
        );
        pmgr_tree_abort(t);
        pmgr_abort_trees();
        exit(1);
    }

    /* check that user's buffer is big enough */
    if (total > recvcount) {
        pmgr_error("Receive buffer is too small to hold incoming data @ file %s:%d",
            __FILE__, __LINE__
        );
        pmgr_tree_abort(t);
        pmgr_abort_trees();
        exit(1);
    }

    /* copy my own data into buffer */
    memcpy(recvbuf, sendbuf, sendcount);

    /* if i have any children, receive their data */
    int i;
    int64_t offset = sendcount;
    for(i = t->num_child-1; i >= 0; i--) {
        /* read number of incoming bytes */
        int64_t incoming;
        if (pmgr_read_collective(t, t->child_fd[i], &incoming, sizeof(int64_t)) < 0) {
            pmgr_error("%s receiving incoming byte count from child %s @ file %s:%d",
                t->name, t->child_name[i], __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }

        /* now receive the data */
        if (pmgr_read_collective(t, t->child_fd[i], recvbuf + offset, incoming) < 0) {
            pmgr_error("%s gathering data from child %s @ file %s:%d",
                t->name, t->child_name[i], __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }

        /* increase our offset */
        offset += incoming;
    }

    /* if i'm not rank 0, send to parent */
    if (t->rank != 0) {
        /* write number of bytes we'll send to parent */
        if (pmgr_write_collective(t, t->parent_fd, &offset, sizeof(int64_t)) < 0) {
            pmgr_error("%s sending byte count to parent %s @ file %s:%d",
                t->name, t->parent_name, __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }

        /* now write the bytes */
        if (pmgr_write_collective(t, t->parent_fd, recvbuf, offset) < 0) {
            pmgr_error("%s sending gathered data to parent %s @ file %s:%d",
                t->name, t->parent_name, __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
    }

    /* finally bcast whole buffer from root */
    if (pmgr_tree_bcast(t, recvbuf, (int) total) != PMGR_SUCCESS) {
        pmgr_error("Bcasting data from rank 0 failed @ file %s:%d",
            __FILE__, __LINE__
        );
        pmgr_tree_abort(t);
        pmgr_abort_trees();
        exit(1);
    }

    /* record number of bytes actually gathered */
    *written = total;

    /* check that everyone succeeded */
    int all_success = pmgr_tree_check(t, 1);
    return all_success;
}

/* alltoall sendcount bytes from each process to each process via tree */
int pmgr_tree_alltoall(pmgr_tree_t* t, void* sendbuf, int sendcount, void* recvbuf)
{
    /* compute total number of bytes we'll receive from children and send to our parent */
    int tmp_recv_count = (t->num_child_incl)   * t->ranks * sendcount;
    int tmp_send_count = (t->num_child_incl+1) * t->ranks * sendcount;

    /* allocate temporary buffers to hold the data */
    void* tmp_recv_buf = NULL;
    void* tmp_send_buf = NULL;
    if (tmp_recv_count > 0) {
        tmp_recv_buf = (void*) pmgr_malloc(tmp_recv_count, "Temporary recv buffer");
    }
    if (tmp_send_count > 0) {
        tmp_send_buf = (void*) pmgr_malloc(tmp_send_count, "Temporary send buffer");
    }

    /* if i have any children, receive their data */
    int i;
    int offset = 0;
    for(i = t->num_child-1; i >= 0; i--) {
        if (pmgr_read_collective(t, t->child_fd[i], (char*)tmp_recv_buf + offset, t->ranks * sendcount * t->child_incl[i]) < 0) {
            pmgr_error("%s gathering data from child %s @ file %s:%d",
                t->name, t->child_name[i], __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
        offset += t->ranks * sendcount * t->child_incl[i];
    }

    /* order data by destination process */
    int j;
    offset = 0;
    for(j = 0; j < t->ranks; j++) {
        /* copy my own data into send buffer */
        memcpy(tmp_send_buf + offset, (char*)sendbuf + sendcount * j, sendcount);
        offset += sendcount;

        /* copy each entry of our child data */
        int child_count = 0;
        for(i = t->num_child-1; i >= 0; i--) {
            memcpy(tmp_send_buf + offset,
                   (char*)tmp_recv_buf + t->ranks * sendcount * child_count + sendcount * j * t->child_incl[i],
                   sendcount * t->child_incl[i]
            );
            offset += sendcount * t->child_incl[i];
            child_count += t->child_incl[i];
        }
    }

    /* if i'm not rank 0, send to parent and free temporary buffer */
    if (t->rank != 0) {
        if (pmgr_write_collective(t, t->parent_fd, tmp_send_buf, tmp_send_count) < 0) {
            pmgr_error("%s sending alltoall data to parent %s @ file %s:%d",
                t->name, t->parent_name, __FILE__, __LINE__
            );
            pmgr_tree_abort(t);
            pmgr_abort_trees();
            exit(1);
        }
    }

    /* scatter data from rank 0 */
    pmgr_tree_scatter(t, tmp_send_buf, sendcount * t->ranks, recvbuf);

    /* free our temporary buffers */
    if (tmp_recv_buf != NULL) {
      free(tmp_recv_buf);
      tmp_recv_buf = NULL;
    }
    if (tmp_send_buf != NULL) {
      free(tmp_send_buf);
      tmp_send_buf = NULL;
    }

    /* check that everyone succeeded */
    int all_success = pmgr_tree_check(t, 1);
    return all_success;
}

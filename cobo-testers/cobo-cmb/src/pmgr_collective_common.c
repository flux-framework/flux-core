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
 * This file provides common implementations for
 *   pmgr_collective_mpirun - the interface used by mpirun
 *   pmgr_collective_client - the interface used by the MPI tasks
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

#include "pmgr_collective_common.h"
#include "pmgr_collective_ranges.h"

/*
   my rank
   -3     ==> unitialized task (may be mpirun or MPI task)
   -2     ==> mpirun
   -1     ==> MPI task before rank is assigned
   0..N-1 ==> MPI task
*/
int pmgr_me = -3;

int pmgr_echo_debug = 0;

static char* pmgr_errstrs[] = {
"0",
"-1",
"PMGR_ERR_POLL",
"PMGR_ERR_POLL_TIMEOUT",
"PMGR_ERR_POLL_HANGUP",
"PMGR_ERR_POLL_EVENT",
"PMGR_ERR_POLL_INVALID_REQ",
"PMGR_ERR_POLL_NOREAD",
"PMGR_ERR_POLL_BAD_READ",
"PMGR_ERR_WRITE_RETURNED_ZERO",
"UNKNOWN",
};

/* return pointer to error string for given error code */
const char* pmgr_errstr(int rc)
{
  /* flip return code to be positive */
  rc *= -1;

  /* check that return code is within valid error range */
  if (rc < 0 || rc >= PMGR_ERR_LAST) {
    rc = PMGR_ERR_LAST;
  }

  /* return pointer to error string corresponding to this code */
  return pmgr_errstrs[rc];
}

/* Return the number of secs as a double between two timeval structs (tv2-tv1) */
double pmgr_getsecs(struct timeval* tv2, struct timeval* tv1)
{
    struct timeval result;
    timersub(tv2, tv1, &result);
    double secs = (double) result.tv_sec + (double) result.tv_usec / 1000000.0;
    return secs;
}

/* Fills in timeval via gettimeofday */
void pmgr_gettimeofday(struct timeval* tv)
{
    if (gettimeofday(tv, NULL) < 0) {
        pmgr_error("Getting time (gettimeofday() %m errno=%d) @ %s:%d",
            errno, __FILE__, __LINE__
        );
    }
}

/* Reads environment variable, bails if not set */
char* pmgr_getenv(char* envvar, int type)
{
    char* str = getenv(envvar);
    if (str == NULL && type == ENV_REQUIRED) {
        pmgr_error("Missing required environment variable: %s @ %s:%d",
            envvar, __FILE__, __LINE__
        );
        exit(1);
    }
    return str;
}

/* malloc n bytes, and bail out with error msg if fails */
void* pmgr_malloc(size_t n, char* msg)
{
    void* p = malloc(n);
    if (!p) {
        pmgr_error("Call to malloc(%lu) failed: %s (%m errno %d) @ %s:%d",
            n, msg, errno, __FILE__, __LINE__
        );
        exit(1);
    }
    return p;
}

/* free memory and set pointer to NULL */
/*
void pmgr_free(void** mem)
{
    if (mem == NULL) {
        return PMGR_FAILURE;
    }
    if (*mem != NULL) {
        free(*mem);
        *mem = NULL;
    }
    return PMGR_SUCCESS;
}
*/

/* print message to stderr */
void pmgr_error(char *fmt, ...)
{
    va_list argp;
    char hostname[256];
    if (gethostname(hostname, 256) < 0) {
        strcpy(hostname, "NULLHOST");
    }
    fprintf(stderr, "PMGR_COLLECTIVE ERROR: ");
    if (pmgr_me >= 0) {
        fprintf(stderr, "rank %d on %s: ", pmgr_me, hostname);
    } else if (pmgr_me == -2) {
        fprintf(stderr, "mpirun on %s: ", hostname);
    } else if (pmgr_me == -1) {
        fprintf(stderr, "unitialized MPI task on %s: ", hostname);
    } else {
        fprintf(stderr, "unitialized task (mpirun or MPI) on %s: ", hostname);
    }
    va_start(argp, fmt);
    vfprintf(stderr, fmt, argp);
    va_end(argp);
    fprintf(stderr, "\n");
}

/* print message to stderr */
void pmgr_debug(int level, char *fmt, ...)
{
    va_list argp;
    char hostname[256];
    if (gethostname(hostname, 256) < 0) {
        strcpy(hostname, "NULLHOST");
    }
    if (pmgr_echo_debug > 0 && pmgr_echo_debug >= level) {
        fprintf(stderr, "PMGR_COLLECTIVE DEBUG: ");
        if (pmgr_me >= 0) {
            fprintf(stderr, "rank %d on %s: ", pmgr_me, hostname);
        } else if (pmgr_me == -2) {
            fprintf(stderr, "mpirun on %s: ", hostname);
        } else if (pmgr_me == -1) {
            fprintf(stderr, "unitialized MPI task on %s: ", hostname);
        } else {
            fprintf(stderr, "unitialized task (mpirun or MPI) on %s: ", hostname);
        }
        va_start(argp, fmt);
        vfprintf(stderr, fmt, argp);
        va_end(argp);
        fprintf(stderr, "\n");
    }
}

/* write size bytes from buf into fd, retry if necessary */
int pmgr_write_fd_suppress(int fd, const void* buf, int size, int suppress)
{
    int rc;
    int n = 0;
    const char* offset = (const char*) buf;

    while (n < size) {
	rc = write(fd, offset, size - n);

	if (rc < 0) {
	    if(errno == EINTR || errno == EAGAIN) {
              continue;
            }
            pmgr_debug(suppress, "Writing to file descriptor (write(fd=%d,offset=%x,size=%d) %m errno=%d) @ file %s:%d",
                fd, offset, size-n, errno, __FILE__, __LINE__
            );
	    return rc;
	} else if(rc == 0) {
            pmgr_debug(suppress, "Unexpected return code of 0 from write to file descriptor (write(fd=%d,offset=%x,size=%d)) @ file %s:%d",
                fd, offset, size-n, __FILE__, __LINE__
            );
	    return PMGR_ERR_WRITE_RETURNED_ZERO;
	}

	offset += rc;
	n += rc;
    }

    return n;
}

/* write size bytes from buf into fd, retry if necessary */
int pmgr_write_fd(int fd, const void* buf, int size)
{
    int rc = pmgr_write_fd_suppress(fd, buf, size, 0);
    return rc;
}

/* read size bytes into buf from fd, retry if necessary */
int pmgr_read_fd_timeout(int fd, void* buf, int size, int msecs)
{
    int rc;
    int n = 0;
    char* offset = (char*) buf;

    struct pollfd fds;
    fds.fd      = fd;
    fds.events  = POLLIN;
    fds.revents = 0x0;

    while (n < size) {
        /* poll the connection with a timeout value */
        int poll_rc = poll(&fds, 1, msecs);
        if (poll_rc < 0) {
	    if(errno == EINTR || errno == EAGAIN) {
              continue;
            }
            pmgr_error("Polling file descriptor for read (read(fd=%d,offset=%x,size=%d) %m errno=%d) @ file %s:%d",
                       fd, offset, size-n, errno, __FILE__, __LINE__
            );
            return PMGR_ERR_POLL;
        } else if (poll_rc == 0) {
            return PMGR_ERR_POLL_TIMEOUT;
        }

        /* check the revents field for errors */
        if (fds.revents & POLLHUP) {
            pmgr_debug(1, "Hang up error on poll for read(fd=%d,offset=%x,size=%d) @ file %s:%d",
                       fd, offset, size-n, __FILE__, __LINE__
            );
            return PMGR_ERR_POLL_HANGUP;
        }

        if (fds.revents & POLLERR) {
            pmgr_debug(1, "Error on poll for read(fd=%d,offset=%x,size=%d) @ file %s:%d",
                       fd, offset, size-n, __FILE__, __LINE__
            );
            return PMGR_ERR_POLL_EVENT;
        }

        if (fds.revents & POLLNVAL) {
            pmgr_error("Invalid request on poll for read(fd=%d,offset=%x,size=%d) @ file %s:%d",
                       fd, offset, size-n, __FILE__, __LINE__
            );
            return PMGR_ERR_POLL_INVALID_REQ;
        }

        if (!(fds.revents & POLLIN)) {
            pmgr_error("No errors found, but POLLIN is not set for read(fd=%d,offset=%x,size=%d) @ file %s:%d",
                       fd, offset, size-n, __FILE__, __LINE__
            );
            return PMGR_ERR_POLL_NOREAD;
        }

        /* poll returned that fd is ready for reading */
	rc = read(fd, offset, size - n);

	if (rc < 0) {
	    if(errno == EINTR || errno == EAGAIN) {
              continue;
            }
            pmgr_error("Reading from file descriptor (read(fd=%d,offset=%x,size=%d) %m errno=%d) @ file %s:%d",
                       fd, offset, size-n, errno, __FILE__, __LINE__
            );
	    return rc;
	} else if(rc == 0) {
            pmgr_debug(1, "Unexpected return code of 0 from read from file descriptor (read(fd=%d,offset=%x,size=%d) revents=%x) @ file %s:%d",
                       fd, offset, size-n, fds.revents, __FILE__, __LINE__
            );
	    return PMGR_ERR_POLL_BAD_READ;
	}

	offset += rc;
	n += rc;
    }

    return n;
}

/* read size bytes into buf from fd, retry if necessary */
int pmgr_read_fd(int fd, void* buf, int size)
{
    /* use in infinite timeout */
    int rc = pmgr_read_fd_timeout(fd, buf, size, -1);
    return rc;
}

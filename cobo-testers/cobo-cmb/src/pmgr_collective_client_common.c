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

#include "pmgr_collective_common.h"
#include "pmgr_collective_ranges.h"
#include "pmgr_collective_client_common.h"

static uint32_t pmgr_serviceid = 2238503211;

/* parameters for connection attempts */
extern int mpirun_connect_tries;
extern int mpirun_connect_timeout;
extern int mpirun_connect_backoff;
extern int mpirun_connect_random;

extern unsigned pmgr_backoff_rand_seed;

extern int mpirun_authenticate_enable;
extern int mpirun_authenticate_timeout;

extern int mpirun_port_scan_timeout;
extern int mpirun_port_scan_connect_timeout;
extern int mpirun_port_scan_connect_attempts;
extern int mpirun_port_scan_connect_sleep;

extern int mpirun_open_timeout;
extern struct timeval time_open;

/* check whether we've exceed the time since open */
int pmgr_have_exceeded_open_timeout()
{
    if (mpirun_open_timeout >= 0) {
        struct timeval time_current;
        pmgr_gettimeofday(&time_current);
        double time_since_open =  pmgr_getsecs(&time_current, &time_open);
        if (time_since_open > (double) mpirun_open_timeout) {
            return 1;
        }
    }
    return 0;
}

/* attempt to flush any write data before closing socket */
int pmgr_shutdown(int fd)
{
#if 0
    /* shutdown the socket (an attempt to flush any write data before closing socket) */
    shutdown(fd, SHUT_WR);

    /* read and throw away all incoming data until we get an error or we find a 0 */
    char buf[1024];
    for(;;) {
        int rc = pmgr_read_fd(fd, buf, sizeof(buf));
        if(rc <= 0) {
            break;
        }
    }
#endif

    return PMGR_SUCCESS;
}

/* Open a connection on socket FD to peer at ADDR (which LEN bytes long).
 * This function uses a non-blocking filedescriptor for the connect(),
 * and then does a bounded poll() for the connection to complete.  This
 * allows us to timeout the connect() earlier than TCP might do it on
 * its own.  We have seen timeouts that failed after several minutes,
 * where we would really prefer to time out earlier and retry the connect.
 *
 * Return 0 on success, -1 for errors.
 */
int pmgr_connect_timeout_suppress(int fd, struct sockaddr_in* addr, int millisec, int suppress)
{
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int err = 0;
    int rc = connect(fd, (struct sockaddr *) addr, sizeof(struct sockaddr_in));
    if (rc < 0 && errno != EINPROGRESS) {
        pmgr_error("Nonblocking connect failed immediately connecting to %s:%d (connect() %m errno=%d) @ file %s:%d",
            inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), errno, __FILE__, __LINE__
        );
        return -1;
    }
    if (rc == 0) {
        goto done;  /* connect completed immediately */
    }

    struct pollfd ufds;
    ufds.fd = fd;
    ufds.events = POLLIN | POLLOUT;
    ufds.revents = 0;

again:	rc = poll(&ufds, 1, millisec);
    if (rc == -1) {
        /* poll failed */
        if (errno == EINTR) {
            /* NOTE: connect() is non-interruptible in Linux */
            goto again;
        } else {
            pmgr_error("Failed to poll connection connecting to %s:%d (poll() %m errno=%d) @ file %s:%d",
                inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), errno, __FILE__, __LINE__
            );
        }
        return -1;
    } else if (rc == 0) {
        /* poll timed out before any socket events */
        pmgr_debug(suppress, "Timedout %d millisec @ file %s:%d",
            millisec, __FILE__, __LINE__
        );
        return -1;
    } else {
        /* poll saw some event on the socket
         * We need to check if the connection succeeded by
         * using getsockopt.  The revent is not necessarily
         * POLLERR when the connection fails! */
        socklen_t err_len = (socklen_t) sizeof(err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) < 0) {
            pmgr_error("Failed to read event on socket connecting to %s:%d (getsockopt() %m errno=%d) @ file %s:%d",
                inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), errno, __FILE__, __LINE__
            );
            return -1; /* solaris pending error */
        }
    }

done:
    fcntl(fd, F_SETFL, flags);

    /* NOTE: Connection refused is typically reported for
     * non-responsive nodes plus attempts to communicate
     * with terminated launcher. */
    if (err) {
        errno=err;
        pmgr_debug(suppress, "Error on socket in pmgr_connect_w_timeout() connecting to %s:%d (getsockopt() set err=%d %m) @ file %s:%d",
            inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), err, __FILE__, __LINE__
        );
        return -1;
    }
 
    return 0;
}

/* Make multiple attempts to connect to given IP:port sleeping for a certain period in between attempts. */
int pmgr_connect_retry(struct in_addr ip, int port, int timeout_millisec, int attempts, int sleep_usecs, int suppress)
{
    struct sockaddr_in sockaddr;
    int sockfd = -1;

    /* set up address to connect to */
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr = ip;
    sockaddr.sin_port = htons(port);

    /* Try making the connection several times, with a random backoff
       between tries. */
    int connected = 0;
    int count = 0;
    while (!connected && count < attempts) {
        /* create a socket */
        sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockfd < 0) {
            pmgr_error("Creating socket (socket() %m errno=%d) @ file %s:%d",
                errno, __FILE__, __LINE__
            );
            return -1;
        }

        /* connect socket to address */
        if (pmgr_connect_timeout_suppress(sockfd, &sockaddr, timeout_millisec, suppress) < 0) {
            if (attempts > 1) {
                close(sockfd);
                if (mpirun_connect_random) {
                  double percentage = ((double)rand_r(&pmgr_backoff_rand_seed)) / ((double)RAND_MAX);
                  useconds_t usecs = (useconds_t)(percentage * sleep_usecs);
                  usleep(usecs);
                } else {
                  usleep(sleep_usecs);
                }
            }
        } else {
            connected = 1;
        }
        count++;
    }

    if (!connected) {
        pmgr_debug(suppress, "Failed to connect to %s:%d @ file %s:%d",
            inet_ntoa(ip), port, __FILE__, __LINE__
        );
        if (sockfd != -1) {
            close(sockfd);
        }
        return -1;
    }

    return sockfd;
}

/* Connect to given IP:port.  Upon successful connection, pmgr_connect
 * shall return the connected socket file descriptor.  Otherwise, -1 shall be
 * returned.
 */
int pmgr_connect(struct in_addr ip, int port)
{
    int timeout_millisec = mpirun_connect_timeout * 1000;
    int sleep_usec       = mpirun_connect_backoff * 1000 * 1000;
    int fd = pmgr_connect_retry(ip, port, timeout_millisec, mpirun_connect_tries, sleep_usec, 1);
    return fd;
}

/* accept a connection, authenticate it, and extract remote IP and port info from socket */
int pmgr_accept(int sockfd, const char* auth, int* out_fd, struct in_addr* out_ip, short* out_port)
{
    /* wait until we have a valid open socket descriptor */
    int fd = -1;
    while (fd == -1) {
        /* accept an incoming connection request */
        struct sockaddr incoming_addr;
        socklen_t incoming_len = sizeof(incoming_addr);
        fd = accept(sockfd, &incoming_addr, &incoming_len);
        if (fd >= 0) {
            /* connected to something, check that it's who we expected to connect to */
            if (pmgr_authenticate_accept(fd, auth, auth, mpirun_authenticate_timeout) == PMGR_SUCCESS) {
                /* connection checks out, now lookup remote address info */
                struct sockaddr_in sin;
                memset(&sin, 0, sizeof(sin));
                socklen_t len = sizeof(sin);
                if (getpeername(fd, (struct sockaddr *) &sin, &len) == 0) {
                    /* extract remote IP and port */
                    struct in_addr ip = sin.sin_addr;
                    short port = ntohs(sin.sin_port);

                    /* fill in IP and port, and return new file descriptor */
                    *out_fd   = fd;
                    *out_ip   = ip;
                    *out_port = port;
                    break;
                } else {
                    pmgr_error("Extracting remote IP and port (getpeername() %m errno=%d) @ file %s:%d",
                        errno, __FILE__, __LINE__
                    );
                    return PMGR_FAILURE;
                }
            } else {
                /* authentication failed, close this socket and accept a new connection */
                close(fd);
                fd = -1;
            }
        } else {
            /* error from accept, try again */
        }
    }
    return PMGR_SUCCESS;
}

/* open a listening socket and return the descriptor, the ip address, and the port */
int pmgr_open_listening_socket(const char* portrange, int portoffset, int* out_fd, struct in_addr* out_ip, short* out_port)
{
    /* create a socket to accept connection from parent */
    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        pmgr_error("Creating parent socket (socket() %m errno=%d) @ file %s:%d",
            errno, __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    struct sockaddr_in sin;
    if (portrange == NULL) {
        /* prepare socket to be bound to ephemeral port - OS will assign us a free port */
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = htonl(INADDR_ANY);
        sin.sin_port = htons(0); /* bind ephemeral port - OS will assign us a free port */

        /* bind socket */
        if (bind(sockfd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
            pmgr_error("Binding parent socket (bind() %m errno=%d) @ file %s:%d",
                errno, __FILE__, __LINE__
            );
            return PMGR_FAILURE;
        }

        /* set the socket to listen for connections */
        if (listen(sockfd, 2) < 0) {
            pmgr_error("Setting parent socket to listen (listen() %m errno=%d) @ file %s:%d",
                errno, __FILE__, __LINE__
            );
            return PMGR_FAILURE;
        }
    } else {
        /* compute number of ports in range */
        int ports;
        pmgr_range_numbers_size(portrange, &ports);

        int i = 1;
        int port_is_bound = 0;
        while (i <= ports && !port_is_bound) {
            /* pick our port index */
            int port_index = portoffset + i;
            while (port_index < 1) {
                port_index += ports;
            }
            while (port_index > ports) {
                port_index -= ports;
            }

            /* select the port number for the index */
            char port_str[1024];
            if (pmgr_range_numbers_nth(portrange, port_index, port_str, sizeof(port_str)) != PMGR_SUCCESS) {
                pmgr_error("Invalid port range string '%s' @ file %s:%d",
                    portrange, __FILE__, __LINE__
                );
                return PMGR_FAILURE;
            }
            int port = atoi(port_str);
            i++;

            /* set up an address using our selected port */
            memset(&sin, 0, sizeof(sin));
            sin.sin_family = AF_INET;
            sin.sin_addr.s_addr = htonl(INADDR_ANY);
            sin.sin_port = htons(port);

            /* attempt to bind a socket on this port */
            if (bind(sockfd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
                pmgr_debug(2, "Binding parent socket (bind() %m errno=%d) port=%d @ file %s:%d",
                    errno, port, __FILE__, __LINE__
                );
                continue;
            }

            /* set the socket to listen for connections */
            if (listen(sockfd, 2) < 0) {
                pmgr_debug(2, "Setting parent socket to listen (listen() %m errno=%d) port=%d @ file %s:%d",
                    errno, port, __FILE__, __LINE__
                );
                continue;
            }

            /* bound and listening on our port */
            pmgr_debug(3, "Opened socket on port %d", port);
            port_is_bound = 1;
        }

        /* print message if we failed to find a port */
        if (!port_is_bound) {
            pmgr_error("Failed to bind socket to port in range '%s' @ file %s:%d",
                portrange, __FILE__, __LINE__
            );
            return PMGR_FAILURE;
        }
    }

    /* ask which port the OS assigned our socket to */
    socklen_t len = sizeof(sin);
    if (getsockname(sockfd, (struct sockaddr *) &sin, &len) < 0) {
        pmgr_error("Reading parent socket port number (getsockname() %m errno=%d) @ file %s:%d",
            errno, __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* extract our ip and port number */
    char hn[256];
    if (gethostname(hn, sizeof(hn)) < 0) {
        pmgr_error("Error calling gethostname() @ file %s:%d",
            __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }
    struct hostent* he = gethostbyname(hn);
    struct in_addr ip = * (struct in_addr *) *(he->h_addr_list);
    short p = ntohs(sin.sin_port);

    /* set output parameters */
    *out_fd   = sockfd;
    *out_ip   = ip;
    *out_port = p;

    return PMGR_SUCCESS;
}

int pmgr_authenticate_accept(int fd, const char* auth_connect, const char* auth_accept, int reply_timeout)
{
    int test_failed = 0;

    /* return right away with success if authentication is disabled */
    if (!mpirun_authenticate_enable) {
        return PMGR_SUCCESS;
    }

    /* get size of connect text */
    uint32_t auth_connect_len = 0;
    if (auth_connect != NULL) {
        auth_connect_len = strlen(auth_connect) + 1;
    }

    /* get size of accept text */
    uint32_t auth_accept_len = 0;
    if (auth_accept != NULL) {
        auth_accept_len = strlen(auth_accept) + 1;
    }

    /* read the service id */
    uint32_t received_serviceid = 0;
    if (!test_failed && pmgr_read_fd_timeout(fd, &received_serviceid, sizeof(received_serviceid), reply_timeout) < 0) {
        pmgr_debug(1, "Receiving service id from new connection failed @ file %s:%d",
            __FILE__, __LINE__
        );
        test_failed = 1;
    }

    /* check that we got the expected service id */
    /* TODO: convert with ntoh32 */
    if (!test_failed && received_serviceid != pmgr_serviceid) {
        test_failed = 1;
    }

    /* read the length of the incoming connect text */
    uint32_t received_connect_len = 0;
    if (!test_failed && pmgr_read_fd_timeout(fd, &received_connect_len, sizeof(received_connect_len), reply_timeout) < 0) {
        pmgr_debug(1, "Receiving length of connect text from new connection failed @ file %s:%d",
            __FILE__, __LINE__
        );
        test_failed = 1;
    }

    /* check that the incoming connect text is the expected length */
    /* TODO: convert with ntoh32 */
    if (received_connect_len != auth_connect_len) {
        test_failed = 1;
    }

    /* read the incoming connect text, if any */
    char* received_connect = NULL;
    if (!test_failed && received_connect_len > 0) {
        /* allocate space to receive incoming connect text */
        received_connect = (char*) malloc(received_connect_len);
        if (received_connect == NULL) {
            pmgr_debug(1, "Failed to allocate memory to receive connect text from new connection @ file %s:%d",
                __FILE__, __LINE__
            );
            test_failed = 1;
        }

        /* receiving the incoming connect text */
        if (!test_failed && pmgr_read_fd_timeout(fd, received_connect, received_connect_len, reply_timeout) < 0) {
            pmgr_debug(1, "Receiving connect text from new connection failed @ file %s:%d",
                __FILE__, __LINE__
            );
            test_failed = 1;
        }
    }

    /* check that we got the expected connect text */
    if (!test_failed && received_connect != NULL) {
        if (strcmp(received_connect, auth_connect) != 0) {
            test_failed = 1;
        }
    }

    /* write a nack back immediately so connecting proc can tear down faster,
     * be sure that the nack value does not match the pmgr_serviceid */
    if (test_failed) {
        /* TODO: convert with hton32 */
        uint32_t nack = 0;
        pmgr_write_fd(fd, &nack, sizeof(nack));
        /* TODO: flush writes */
    }

    /* write our service id back as a reply */
    /* TODO: convert with hton32 */
    if (!test_failed && pmgr_write_fd_suppress(fd, &pmgr_serviceid, sizeof(pmgr_serviceid), 1) < 0) {
        pmgr_debug(1, "Writing service id to new connection failed @ file %s:%d",
                   __FILE__, __LINE__
        );
        test_failed = 1;
    }

    /* write the length of our accept text back as a reply */
    /* TODO: convert with hton32 */
    if (!test_failed && pmgr_write_fd_suppress(fd, &auth_accept_len, sizeof(auth_accept_len), 1) < 0) {
        pmgr_debug(1, "Writing length of accept text to new connection failed @ file %s:%d",
                   __FILE__, __LINE__
        );
        test_failed = 1;
    }

    /* write our accept text back as a reply */
    if (!test_failed && pmgr_write_fd_suppress(fd, auth_accept, auth_accept_len, 1) < 0) {
        pmgr_debug(1, "Writing accept text to new connection failed @ file %s:%d",
                   __FILE__, __LINE__
        );
        test_failed = 1;
    }

    /* force our writes to be sent */
    if (!test_failed) {
        /* TODO: flush writes */
    }

    /* the other end may have dropped us if it was too impatient waiting for our reply,
     * read its ack to know that it completed the connection */
    uint32_t ack = 0;
    if (!test_failed && pmgr_read_fd_timeout(fd, &ack, sizeof(ack), reply_timeout) < 0) {
        pmgr_debug(1, "Receiving ack to finalize connection @ file %s:%d",
                   __FILE__, __LINE__
        );
        test_failed = 1;
    }

    /* if we allocated any memory, free it off */
    if (received_connect != NULL) {
        free(received_connect);
        received_connect = NULL;
    }

    /* return our verdict */
    if (test_failed) {
        return PMGR_FAILURE;
    }
    return PMGR_SUCCESS;
}

/* issues a handshake across connection to verify we really connected to the right socket */
int pmgr_authenticate_connect(int fd, const char* auth_connect, const char* auth_accept, int reply_timeout)
{
    int test_failed = 0;

    /* return right away with success if authentication is disabled */
    if (!mpirun_authenticate_enable) {
        return PMGR_SUCCESS;
    }

    /* get size of connect text */
    uint32_t auth_connect_len = 0;
    if (auth_connect != NULL) {
        auth_connect_len = strlen(auth_connect) + 1;
    }

    /* get size of accept text */
    uint32_t auth_accept_len = 0;
    if (auth_accept != NULL) {
        auth_accept_len = strlen(auth_accept) + 1;
    }

    /* write pmgr service id */
    /* TODO: convert with hton32 */
    if (!test_failed && pmgr_write_fd_suppress(fd, &pmgr_serviceid, sizeof(pmgr_serviceid), 1) < 0) {
        pmgr_debug(1, "Failed to write service id @ file %s:%d",
            __FILE__, __LINE__
        );
        test_failed = 1;
    }

    /* write length of our connect text */
    /* TODO: convert with hton32 */
    if (!test_failed && pmgr_write_fd_suppress(fd, &auth_connect_len, sizeof(auth_connect_len), 1) < 0) {
       pmgr_debug(1, "Failed to write length of connect text @ file %s:%d",
           __FILE__, __LINE__
       );
       test_failed = 1;
    }

    /* write our connect text */
    if (!test_failed && pmgr_write_fd_suppress(fd, auth_connect, auth_connect_len, 1) < 0) {
       pmgr_debug(1, "Failed to write connect text @ file %s:%d",
           __FILE__, __LINE__
       );
       test_failed = 1;
    }

    /* force our writes to be sent */
    if (!test_failed) {
        /* TODO: flush writes */
    }

    /* read the pmgr service id */
    uint32_t received_serviceid = 0;
    if (!test_failed && pmgr_read_fd_timeout(fd, &received_serviceid, sizeof(received_serviceid), reply_timeout) < 0) {
        pmgr_debug(1, "Failed to receive service id @ file %s:%d",
            __FILE__, __LINE__
        );
        test_failed = 1;
    }

    /* check that we got the expected service id */
    /* TODO: convert with ntoh32 */
    if (!test_failed && received_serviceid != pmgr_serviceid) {
        pmgr_debug(1, "Received invalid service id @ file %s:%d",
            __FILE__, __LINE__
        );
        test_failed = 1;
    }

    /* read the length of the incoming accept text */
    uint32_t received_accept_len = 0;
    if (!test_failed && pmgr_read_fd_timeout(fd, &received_accept_len, sizeof(received_accept_len), reply_timeout) < 0) {
        pmgr_debug(1, "Receiving length of connect text from new connection failed @ file %s:%d",
            __FILE__, __LINE__
        );
        test_failed = 1;
    }

    /* check that the incoming accept text length matches the expected length */
    /* TODO: convert with ntoh32 */
    if (received_accept_len != auth_accept_len) {
        test_failed = 1;
    }

    /* read the accept text */
    char* received_accept = NULL;
    if (!test_failed && auth_accept_len > 0) {
        /* allocate space to hold incoming accept text */
        received_accept = (char*) malloc(received_accept_len);
        if (received_accept == NULL) {
            pmgr_debug(1, "Failed to allocate memory to receive accept text @ file %s:%d",
                __FILE__, __LINE__
            );
            test_failed = 1;
        }

        /* read incoming accept text */
        if (!test_failed && pmgr_read_fd_timeout(fd, received_accept, received_accept_len, reply_timeout) < 0) {
            pmgr_debug(1, "Failed to receive accept text @ file %s:%d",
                __FILE__, __LINE__
            );
            test_failed = 1;
        }
    }

    /* check that we got the expected accept text */
    if (!test_failed && received_accept != NULL) {
        if (strcmp(received_accept, auth_accept) != 0) {
            pmgr_debug(1, "Received invalid accept text @ file %s:%d",
                __FILE__, __LINE__
            );
            test_failed = 1;
        }
    }

    /* write ack to finalize connection (no need to suppress write errors any longer) */
    unsigned int ack = 1;
    if (!test_failed && pmgr_write_fd(fd, &ack, sizeof(ack)) < 0) {
        pmgr_debug(1, "Failed to write ACK to finalize connection @ file %s:%d",
            __FILE__, __LINE__
        );
        test_failed = 1;
    }

    /* force our writes to be sent */
    if (!test_failed) {
        /* TODO: flush writes */
    }

    /* if we allocated memory, free it off */
    if (received_accept != NULL) {
        free(received_accept);
        received_accept = NULL;
    }

    /* return our verdict */
    if (test_failed) {
        return PMGR_FAILURE;
    }
    return PMGR_SUCCESS;
}

/* Attempts to connect to a given hostname using a port list and timeouts */
int pmgr_connect_hostname(
    int rank, const char* hostname, const char* portrange, int portoffset,
    const char* auth_connect, const char* auth_accept,
    int* out_fd, struct in_addr* out_ip, short* out_port)
{
    int s = -1;

    double timelimit  = (double) mpirun_port_scan_timeout; /* seconds */
    int timeout       = mpirun_port_scan_connect_timeout;
    int attempts      = mpirun_port_scan_connect_attempts;
    int sleep         = mpirun_port_scan_connect_sleep * 1000; /* convert msecs to usecs */
    int reply_timeout = mpirun_authenticate_timeout;
    int suppress      = 3;

    /* allow our timeouts to increase dynamically,
     * arbitrarily pick 100x the base as the range */
    //int max_timeout       = timeout * 128;
    int max_timeout       = timeout * 1;

    /* lookup host address by name */
    struct hostent* he = gethostbyname(hostname);
    if (!he) {
        pmgr_error("Hostname lookup failed (gethostbyname(%s) %s h_errno=%d) @ file %s:%d",
                   hostname, hstrerror(h_errno), h_errno, __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    }

    /* set ip address */
    struct in_addr ip = *(struct in_addr *) *(he->h_addr_list);

    /* get number of ports */
    int ports = 0;
    pmgr_range_numbers_size(portrange, &ports);

    /* allocate space to read port value into */
    char* target_port = strdup(portrange);
    size_t target_port_len = strlen(target_port) + 1;

    /* Loop until we make a connection or until our timeout expires. */
    int port = 0;
    struct timeval start, end;
    pmgr_gettimeofday(&start);
    double secs = 0;
    int connected = 0;
    while (!connected && (timelimit < 0 || secs < timelimit)) {
        /* iterate over our ports trying to find a connection */
        int i;
        for (i = 1; i <= ports; i++) {
            /* select the index of the next port */
            int port_index = portoffset + i;
            while (port_index < 1) {
                port_index += ports;
            }
            while (port_index > ports) {
                port_index -= ports;
            }

            /* select the port number corresponding to the current port index */
            if (pmgr_range_numbers_nth(portrange, port_index, target_port, target_port_len) == PMGR_SUCCESS) {
                port = atoi(target_port);
            } else {
                continue;
            }

            /* attempt to connect to hostname on this port */
            pmgr_debug(3, "Trying rank %d on port %d on %s", rank, port, hostname);
            s = pmgr_connect_retry(ip, port, timeout, attempts, sleep, suppress);
            if (s != -1) {
                /* got a connection, let's test it out */
                pmgr_debug(2, "Connected to rank %d port %d on %s", rank, port, hostname);

                if (pmgr_authenticate_connect(s, auth_connect, auth_accept, reply_timeout) == PMGR_SUCCESS)
                {
                    /* it checks out, we're connected to the right process */
                    connected = 1;
                    break;
                } else {
                    /* don't know who we connected to, close the socket */
                    close(s);
                }
            }

            /* sleep before we try another port */
            usleep(sleep);
        }

        /* before we try another port scan, sleep or increase our timeouts */
        if (!connected) {
            /* sleep for some time before we try another port scan */
            //usleep(mpirun_port_scan_connect_sleep);

            /* extent the time we wait for a connection, higher times decrease the amount of IP packets but increase
             * the time it takes to scan the port range */
            if (mpirun_port_scan_connect_timeout >= 0) {
              if (timeout*2 < max_timeout) {
                timeout *= 2;
              } else {
                timeout = max_timeout;
              }
            }
        }

        /* compute how many seconds we've spent trying to connect */
        pmgr_gettimeofday(&end);
        secs = pmgr_getsecs(&end, &start);
        if (timelimit >= 0 && secs >= timelimit) {
            pmgr_error("Time limit to connect to rank %d on %s expired (%f secs) @ file %s:%d",
                       rank, hostname, timelimit, __FILE__, __LINE__
            );
        }
    }

    /* free space allocated to hold port value */
    if (target_port != NULL) {
        free(target_port);
        target_port = NULL;
    }

    /* check that we successfully opened a socket */
    if (s == -1) {
        pmgr_error("Connecting socket to %s at %s failed @ file %s:%d",
                   he->h_name, inet_ntoa(ip),
                   __FILE__, __LINE__
        );
        return PMGR_FAILURE;
    } else {
        /* inform caller of ip address and port that we're connected to */
        *out_fd   = s;
        *out_ip   = ip;
        *out_port = (short) port;
        return PMGR_SUCCESS;
    }
}

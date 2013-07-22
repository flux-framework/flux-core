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

#ifndef _PMGR_COLLECTIVE_CLIENT_COMMON_H
#define _PMGR_COLLECTIVE_CLIENT_COMMON_H

/* check whether we've exceed the time since open */
int pmgr_have_exceeded_open_timeout();

/* attempt to flush any write data before closing socket */
int pmgr_shutdown(int fd);

/* Open a connection on socket FD to peer at ADDR (which LEN bytes long).
 * This function uses a non-blocking filedescriptor for the connect(),
 * and then does a bounded poll() for the connection to complete.  This
 * allows us to timeout the connect() earlier than TCP might do it on
 * its own.  We have seen timeouts that failed after several minutes,
 * where we would really prefer to time out earlier and retry the connect.
 *
 * Return 0 on success, -1 for errors.
 */
int pmgr_connect_timeout_suppress(int fd, struct sockaddr_in* addr, int millisec, int suppress);

/* Make multiple attempts to connect to given IP:port sleeping for a certain period in between attempts. */
int pmgr_connect_retry(struct in_addr ip, int port, int timeout_millisec, int attempts, int sleep_usecs, int suppress);

/* Connect to given IP:port.  Upon successful connection, pmgr_connect
 * shall return the connected socket file descriptor.  Otherwise, -1 shall be
 * returned.
 */
int pmgr_connect(struct in_addr ip, int port);

/* accept a connection, authenticate it, and extract remote IP and port info from socket */
int pmgr_accept(int sockfd, const char* auth, int* out_fd, struct in_addr* out_ip, short* out_port);

/* open a listening socket and return the descriptor, the ip address, and the port */
int pmgr_open_listening_socket(const char* portrange, int portoffset, int* out_fd, struct in_addr* out_ip, short* out_port);

int pmgr_authenticate_accept(int fd, const char* auth_connect, const char* auth_accept, int reply_timeout);

/* issues a handshake across connection to verify we really connected to the right socket */
int pmgr_authenticate_connect(int fd, const char* auth_connect, const char* auth_accept, int reply_timeout);

/* Attempts to connect to a given hostname using a port list and timeouts */
int pmgr_connect_hostname(
    int rank, const char* hostname, const char* portrange, int portoffset, const char* auth_connect, const char* auth_accept,
    int* fd, struct in_addr* ip, short* port
);

#endif /* _PMGR_COLLECTIVE_CLIENT_COMMON_H */


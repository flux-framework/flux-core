/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/* The Flux public, versioned API.
 */

#ifndef _FLUX_CORE_H
#define _FLUX_CORE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct flux_struct *flux_t;
typedef struct flux_msg_struct *flux_msg_t;

/* Open flags
 */
enum {
    FLUX_O_TRACE        = 1,    /* send message trace to stderr */
    FLUX_O_COPROC       = 2,    /* start reactor callbacks as coprocesses */
};

/* sendmsg/recvmsg/putmsg flags.
 */
enum {
    FLUX_IO_NONBLOCK    = 1,
    FLUX_IO_PUT_BEGIN   = 2,    /* putmsg adds to front of receive queue */
    FLUX_IO_PUT_END     = 4,    /* putmsg adds to end of receive queue */
};

/* Create/destroy a broker handle.
 * If 'path' is NULL, derive socket path from FLUX_TMPDIR
 * environment variable;  otherwise it can be a zeromq URI or a socket path.
 * On error, NULL is returned with ernno set.
 */
flux_t flux_open (const char *path, int flags);
void flux_close (flux_t h);

/* Send/recv Flux messages.
 * Putmsg adds 'msg' to the handle's recveive queue.
 * On success return 0; on failure return -1 with errno set.
 */
int flux_sendmsg (flux_t h, flux_msg_t msg, int flags);
flux_msg_t flux_recvmsg (flux_t h, int flags);
int flux_putmsg (flux_t h, flux_msg_t msg, int flags);

/* Subscribe/unsubscribe to events.  A NULL topic_glob matches all events.
 * On success return 0; on failure return -1 with errno set.
 */
int flux_subscribe (flux_t h, const char *topic_glob);
int flux_unsubscribe (flux_t h, const char *topic_glob);

/* Publish one event message.
 * If non-NULL, 'json_in' is the payload.
 * On success return 0; on failure return -1 with errno set.
 */
int flux_publish (flux_t h, const char *topic, const char *json_in);

/* Send one request message.
 * The value of nodeid can be a broker rank, FLUX_NODEID_ANY,
 * or FLUX_NODEID_UPSTREAM and affects request routing per RFC 3.
 * If non-NULL, 'json_in' is the payload.
 * On success return 0; on failure return -1 with errno set.
 */
int flux_request (flux_t h, uint32_t nodeid, const char *topic,
                  const char *json_in);

/* Send one request message and receive one response message.
 * The value of 'nodeid' can be a broker rank, FLUX_NODEID_ANY,
 * or FLUX_NODEID_UPSTREAM and affects request routing per RFC 3.
 * If non-NULL, 'json_in' is the request payload.
 * If a response payload is expected, 'json_out' must be non-NULL
 * (caller frees the returned string).
 * If timeout is nonzero, the RPC should return with ETIMEDOUT if
 * more than the specified number of milliseconds elapses before a
 * response is received.
 * On success return 0; on failure return -1 with errno set.
 */
int flux_rpc (flux_t h, uint32_t nodeid, const char *topic,
              const char *json_in, char **json_out, int timeout);


#endif /* !_FLUX_CORE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

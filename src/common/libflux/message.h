/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_MESSAGE_H
#define _FLUX_CORE_MESSAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FLUX_MSGTYPE_REQUEST    = 0x01,
    FLUX_MSGTYPE_RESPONSE   = 0x02,
    FLUX_MSGTYPE_EVENT      = 0x04,
    FLUX_MSGTYPE_CONTROL    = 0x08,
    FLUX_MSGTYPE_ANY        = 0x0f,
    FLUX_MSGTYPE_MASK       = 0x0f,
};

enum {
    FLUX_MSGFLAG_TOPIC      = 0x01, /* message has topic string */
    FLUX_MSGFLAG_PAYLOAD    = 0x02, /* message has payload */
    FLUX_MSGFLAG_NORESPONSE = 0x04, /* request needs no response */
    FLUX_MSGFLAG_ROUTE      = 0x08, /* message is routable */
    FLUX_MSGFLAG_UPSTREAM   = 0x10, /* request nodeid is sender (route away) */
    FLUX_MSGFLAG_PRIVATE    = 0x20, /* private to instance owner and sender */
    FLUX_MSGFLAG_STREAMING  = 0x40, /* request/response is streaming RPC */
    FLUX_MSGFLAG_USER1      = 0x80, /* user-defined message flag */
};

/* N.B. FLUX_NODEID_UPSTREAM should be used in the RPC interface only.
 * The resulting request message is constructed with flags including
 * FLUX_MSGFLAG_UPSTREAM and nodeid set set to local broker rank.
 */
enum {
    FLUX_NODEID_ANY      = 0xFFFFFFFF, //(~(uint32_t)0),
    FLUX_NODEID_UPSTREAM = 0xFFFFFFFE  //(~(uint32_t)1)
};

struct flux_match {
    int typemask;           /* bitmask of matching message types (or 0) */
    uint32_t matchtag;      /* matchtag (or FLUX_MATCHTAG_NONE) */
    const char *topic_glob; /* glob matching topic string (or NULL) */
};

struct flux_match flux_match_init (int typemask,
                                   uint32_t matchtag,
                                   const char *topic_glob);

void flux_match_free (struct flux_match m);

int flux_match_asprintf (struct flux_match *m,
                         const char *topic_glob_fmt,
                         ...);

#define FLUX_MATCH_ANY flux_match_init( \
    FLUX_MSGTYPE_ANY, \
    FLUX_MATCHTAG_NONE, \
    NULL \
)
#define FLUX_MATCH_EVENT flux_match_init( \
    FLUX_MSGTYPE_EVENT, \
    FLUX_MATCHTAG_NONE, \
    NULL \
)
#define FLUX_MATCH_REQUEST flux_match_init( \
    FLUX_MSGTYPE_REQUEST, \
    FLUX_MATCHTAG_NONE, \
    NULL \
)
#define FLUX_MATCH_RESPONSE flux_match_init( \
    FLUX_MSGTYPE_RESPONSE, \
    FLUX_MATCHTAG_NONE, \
    NULL \
)

/* Create a new Flux message.  If the type of the message is not yet
 * known at creation time, FLUX_MSGTYPE_ANY can be used.
 *
 * Returns new message or null on failure, with errno set (e.g. ENOMEM, EINVAL)
 * Caller must destroy message with flux_msg_destroy() or equivalent.
 */
flux_msg_t *flux_msg_create (int type);
void flux_msg_destroy (flux_msg_t *msg);

/* Access auxiliary data members in Flux message.
 * These are for convenience only - they are not sent over the wire.
 */
int flux_msg_aux_set (const flux_msg_t *msg,
                      const char *name,
                      void *aux,
                      flux_free_f destroy);
void *flux_msg_aux_get (const flux_msg_t *msg, const char *name);

/* Duplicate msg, omitting payload if 'payload' is false.
 */
flux_msg_t *flux_msg_copy (const flux_msg_t *msg, bool payload);

/* Manipulate msg reference count..
 */
const flux_msg_t *flux_msg_incref (const flux_msg_t *msg);
void flux_msg_decref (const flux_msg_t *msg);

/* Encode a flux_msg_t to buffer (pre-sized by calling flux_msg_encode_size()).
 * Returns 0 on success, -1 on failure with errno set.
 */
ssize_t flux_msg_encode_size (const flux_msg_t *msg);
int flux_msg_encode (const flux_msg_t *msg, void *buf, size_t size);

/* Decode a flux_msg_t from buffer.
 * Returns message on success, NULL on failure with errno set.
 * Caller must destroy message with flux_msg_destroy().
 */
flux_msg_t *flux_msg_decode (const void *buf, size_t size);

/* Get/set message type
 * For FLUX_MSGTYPE_REQUEST: set_type initializes nodeid to FLUX_NODEID_ANY
 * For FLUX_MSGTYPE_RESPONSE: set_type initializes errnum to 0
 */
int flux_msg_set_type (flux_msg_t *msg, int type);
int flux_msg_get_type (const flux_msg_t *msg, int *type);

/* Get/set privacy flag.
 * Broker will not route a private message to connections not
 * authenticated as message sender or with instance owner role.
 */
int flux_msg_set_private (flux_msg_t *msg);
bool flux_msg_is_private (const flux_msg_t *msg);

/* Get/set streaming flag.
 * Requests to streaming RPC services should set this flag.
 * Streaming RPC services should return an error if flag is not set.
 */
int flux_msg_set_streaming (flux_msg_t *msg);
bool flux_msg_is_streaming (const flux_msg_t *msg);

/* Get/set noresponse flag.
 * Request is advisory and should not receive a response.
 */
int flux_msg_set_noresponse (flux_msg_t *msg);
bool flux_msg_is_noresponse (const flux_msg_t *msg);

/* Get/set/compare message topic string.
 * set adds/deletes/replaces topic frame as needed.
 */
int flux_msg_set_topic (flux_msg_t *msg, const char *topic);
int flux_msg_get_topic (const flux_msg_t *msg, const char **topic);

/* Get/set payload.
 * Set function adds/deletes/replaces payload frame as needed.
 * The new payload will be copied (caller retains ownership).
 * Any old payload is deleted.
 * flux_msg_get_payload returns pointer to msg-owned buf.
 */
int flux_msg_get_payload (const flux_msg_t *msg,
                          const void **buf,
                          size_t *size);
int flux_msg_set_payload (flux_msg_t *msg, const void *buf, size_t size);
bool flux_msg_has_payload (const flux_msg_t *msg);

/* Test/set/clear message flags
 */
bool flux_msg_has_flag (const flux_msg_t *msg, int flag);
int flux_msg_set_flag (flux_msg_t *msg, int flag);
int flux_msg_clear_flag (flux_msg_t *msg, int flag);

/* Get/set string payload.
 * flux_msg_set_string() accepts a NULL 's' (no payload).
 * flux_msg_get_string() will set 's' to NULL if there is no payload
 * N.B. the raw payload includes C string \0 terminator.
 */
int flux_msg_set_string (flux_msg_t *msg, const char *);
int flux_msg_get_string (const flux_msg_t *msg, const char **s);

/* Get/set JSON payload (encoded as string)
 * pack/unpack functions use jansson pack/unpack style arguments for
 * encoding/decoding the JSON object payload directly from/to its members.
 */
int flux_msg_pack (flux_msg_t *msg, const char *fmt, ...);
int flux_msg_vpack (flux_msg_t *msg, const char *fmt, va_list ap);

int flux_msg_unpack (const flux_msg_t *msg, const char *fmt, ...);
int flux_msg_vunpack (const flux_msg_t *msg, const char *fmt, va_list ap);

/* Return a string representation of the last error encountered for `msg`.
 *
 * If no last error is available, an empty string will be returned.
 *
 * Currently, only flux_msg_pack/unpack() (and related) functions will set
 * the last error for `msg`. (Useful to get more detail from EPROTO errors)
 */
const char *flux_msg_last_error (const flux_msg_t *msg);

/* Get/set nodeid (request only)
 */
int flux_msg_set_nodeid (flux_msg_t *msg, uint32_t nodeid);
int flux_msg_get_nodeid (const flux_msg_t *msg, uint32_t *nodeid);

/* Get/set userid
 */
enum {
    FLUX_USERID_UNKNOWN = 0xFFFFFFFF
};
int flux_msg_set_userid (flux_msg_t *msg, uint32_t userid);
int flux_msg_get_userid (const flux_msg_t *msg, uint32_t *userid);

/* Get/set rolemask
 */
enum {
    FLUX_ROLE_NONE = 0,
    FLUX_ROLE_OWNER = 1,
    FLUX_ROLE_USER = 2,
    FLUX_ROLE_LOCAL = 4,
    FLUX_ROLE_ALL = 0xFFFFFFFF,
};
int flux_msg_set_rolemask (flux_msg_t *msg, uint32_t rolemask);
int flux_msg_get_rolemask (const flux_msg_t *msg, uint32_t *rolemask);

/* Combined rolemask, userid access for convenience
 */
struct flux_msg_cred {
    uint32_t userid;
    uint32_t rolemask;
};
int flux_msg_get_cred (const flux_msg_t *msg, struct flux_msg_cred *cred);
int flux_msg_set_cred (flux_msg_t *msg, struct flux_msg_cred cred);

/* Simple authorization for service access:
 * If cred rolemask includes OWNER, grant (return 0).
 * If cred rolemask includes USER and userid matches 'userid',
 * and userid is not FLUX_USERID_UNKNOWN, grant (return 0).
 * Otherwise deny (return -1, errno = EPERM).
 */
int flux_msg_cred_authorize (struct flux_msg_cred cred, uint32_t userid);

/* Convenience function that calls
 * flux_msg_get_cred() + flux_msg_cred_authorize().
 */
int flux_msg_authorize (const flux_msg_t *msg, uint32_t userid);

/* Return true if 'msg' credential carries FLUX_ROLE_LOCAL, indicating
 * that the message has not traversed brokers.
 */
bool flux_msg_is_local (const flux_msg_t *msg);

/* Get/set errnum (response only)
 */
int flux_msg_set_errnum (flux_msg_t *msg, int errnum);
int flux_msg_get_errnum (const flux_msg_t *msg, int *errnum);

/* Get/set sequence number (event only)
 */
int flux_msg_set_seq (flux_msg_t *msg, uint32_t seq);
int flux_msg_get_seq (const flux_msg_t *msg, uint32_t *seq);

/* Get/set type, status (control only)
 */
int flux_msg_set_control (flux_msg_t *msg, int type, int status);
int flux_msg_get_control (const flux_msg_t *msg, int *type, int *status);

/* Get/set/compare match tag (request/response only)
 */
enum {
    FLUX_MATCHTAG_NONE = 0,
};
int flux_msg_set_matchtag (flux_msg_t *msg, uint32_t matchtag);
int flux_msg_get_matchtag (const flux_msg_t *msg, uint32_t *matchtag);
bool flux_msg_cmp_matchtag (const flux_msg_t *msg, uint32_t matchtag);

/* Match a message.
 */
bool flux_msg_cmp (const flux_msg_t *msg, struct flux_match match);

/* Print a Flux message on specified output stream.
 */
void flux_msg_fprint (FILE *f, const flux_msg_t *msg);
void flux_msg_fprint_ts (FILE *f, const flux_msg_t *msg, double timestamp);

/* Convert a numeric FLUX_MSGTYPE value to string,
 * or "unknown" if unrecognized.
 */
const char *flux_msg_typestr (int type);

/* NOTE: routing frames are pushed on a message traveling dealer
 * to router, and popped off a message traveling router to dealer.
 * A message intended for dealer-router sockets must first be enabled for
 * routing.
 */

/* Enable routes on a message by setting FLUX_MSGFLAG_ROUTE.  This
 * function is a no-op if the flag is already set.
 */
void flux_msg_route_enable (flux_msg_t *msg);

/* Disable routes on a message by clearing the FLUX_MSGFLAG_ROUTE
 * flag.  In addition, clear all presently stored routes.  This
 * function is a no-op if the flag is already set.
 */
void flux_msg_route_disable (flux_msg_t *msg);

/* Clear all routes stored in a message.  This function is a no-op if
 * routes are not enabled.
 */
void flux_msg_route_clear (flux_msg_t *msg);

/* Push a route frame onto the message (mimic what dealer socket does).
 * 'id' is copied internally.
 * Returns 0 on success, -1 with errno set (e.g. EINVAL) on failure.
 */
int flux_msg_route_push (flux_msg_t *msg, const char *id);

/* Delete last route frame off the message.  Effectively performs the
 * "opposite" of flux_msg_route_push().
 *
 * Returns 0 on success, -1 with errno set (e.g. EPROTO) on failure.
 */
int flux_msg_route_delete_last (flux_msg_t *msg);

/* Return the first route (e.g. first pushed route) or NULL if there
 * are no routes.
 * For requests, this is the sender; for responses, this is the recipient.
 * Returns route id on success, NULL for no route or error.
 */
const char *flux_msg_route_first (const flux_msg_t *msg);

/* Return the last route (e.g. most recent pushed route) or NULL if there
 * are no routes.
 * For requests, this is the last hop; for responses: this is the next hop.
 * Returns route id on success, NULL for no route or error.
 */
const char *flux_msg_route_last (const flux_msg_t *msg);

/* Return the number of route frames in the message.
 * It is an EPROTO error if there is no route stack.
 * Returns 0 on success, -1 with errno set (e.g. EPROTO) on failure.
 */
int flux_msg_route_count (const flux_msg_t *msg);

/* Return a string representing the route stack in message.
 * Return NULL if routes are not enabled; empty string if
 * the route stack contains no route frames).
 * Caller must free the returned string.
 */
char *flux_msg_route_string (const flux_msg_t *msg);

/* Return true if messages have the same first routing frame.
 * (For requests, the sender)
 */
bool flux_msg_route_match_first (const flux_msg_t *msg1,
                                 const flux_msg_t *msg2);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_MESSAGE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */


#ifndef _FLUX_CORE_MESSAGE_H
#define _FLUX_CORE_MESSAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include "types.h"
#include "security.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flux_msg flux_msg_t;
//typedef struct _zmsg_t flux_msg_t;

enum {
    FLUX_MSGTYPE_REQUEST    = 0x01,
    FLUX_MSGTYPE_RESPONSE   = 0x02,
    FLUX_MSGTYPE_EVENT      = 0x04,
    FLUX_MSGTYPE_KEEPALIVE  = 0x08,
    FLUX_MSGTYPE_ANY        = 0x0f,
    FLUX_MSGTYPE_MASK       = 0x0f,
};

enum {
    FLUX_MSGFLAG_TOPIC      = 0x01,	/* message has topic string */
    FLUX_MSGFLAG_PAYLOAD    = 0x02,	/* message has payload */
    FLUX_MSGFLAG_JSON       = 0x04,	/* message payload is JSON */
    FLUX_MSGFLAG_ROUTE      = 0x08,	/* message is routable */
    FLUX_MSGFLAG_UPSTREAM   = 0x10, /* request nodeid is sender (route away) */
    FLUX_MSGFLAG_PRIVATE    = 0x20, /* private to instance owner and sender */
};

struct flux_match {
    int typemask;           /* bitmask of matching message types (or 0) */
    uint32_t matchtag;      /* matchtag (or FLUX_MATCHTAG_NONE) */
    char *topic_glob;       /* glob matching topic string (or NULL) */
};

#define FLUX_MATCH_ANY (struct flux_match){ \
    .typemask = FLUX_MSGTYPE_ANY, \
    .matchtag = FLUX_MATCHTAG_NONE, \
    .topic_glob = NULL, \
}
#define FLUX_MATCH_EVENT (struct flux_match){ \
    .typemask = FLUX_MSGTYPE_EVENT, \
    .matchtag = FLUX_MATCHTAG_NONE, \
    .topic_glob = NULL, \
}
#define FLUX_MATCH_REQUEST (struct flux_match){ \
    .typemask = FLUX_MSGTYPE_REQUEST, \
    .matchtag = FLUX_MATCHTAG_NONE, \
    .topic_glob = NULL, \
}
#define FLUX_MATCH_RESPONSE (struct flux_match){ \
    .typemask = FLUX_MSGTYPE_RESPONSE, \
    .matchtag = FLUX_MATCHTAG_NONE, \
    .topic_glob = NULL, \
}

struct flux_msg_iobuf {
    uint8_t *buf;
    size_t size;
    size_t done;
    uint8_t buf_fixed[4096];
};

/* Create a new Flux message.
 * Returns new message or null on failure, with errno set (e.g. ENOMEM, EINVAL)
 * Caller must destroy message with flux_msg_destroy() or equivalent.
 */
flux_msg_t *flux_msg_create (int type);
void flux_msg_destroy (flux_msg_t *msg);

/* Access auxiliary data members in Flux message.
 * These are for convenience only - they are not sent over the wire.
 */
int flux_msg_aux_set (const flux_msg_t *msg, const char *name,
                      void *aux, flux_free_f destroy);
void *flux_msg_aux_get (const flux_msg_t *msg, const char *name);

/* Duplicate msg, omitting payload if 'payload' is false.
 */
flux_msg_t *flux_msg_copy (const flux_msg_t *msg, bool payload);

/* Encode a flux_msg_t to buffer (pre-sized by calling flux_msg_encode_size()).
 * Returns 0 on success, -1 on failure with errno set.
 */
size_t flux_msg_encode_size (const flux_msg_t *msg);
int flux_msg_encode (const flux_msg_t *msg, void *buf, size_t size);

/* Get the number of message frames in 'msg'.
 */
int flux_msg_frames (const flux_msg_t *msg);

/* Decode a flux_msg_t from buffer.
 * Returns message on success, NULL on failure with errno set.
 * Caller must destroy message with flux_msg_destroy().
 */
flux_msg_t *flux_msg_decode (const void *buf, size_t size);

/* Send message to file descriptor.
 * iobuf captures intermediate state to make EAGAIN/EWOULDBLOCK restartable.
 * Returns 0 on success, -1 on failure with errno set.
 */
int flux_msg_sendfd (int fd, const flux_msg_t *msg,
                     struct flux_msg_iobuf *iobuf);

/* Receive message from file descriptor.
 * iobuf captures intermediate state to make EAGAIN/EWOULDBLOCK restartable.
 * Returns message on success, NULL on failure with errno set.
 */
flux_msg_t *flux_msg_recvfd (int fd, struct flux_msg_iobuf *iobuf);

/* Send message to zeromq socket.
 * Returns 0 on success, -1 on failure with errno set.
 */
int flux_msg_sendzsock (void *dest, const flux_msg_t *msg);
int flux_msg_sendzsock_munge (void *sock, const flux_msg_t *msg,
                              flux_sec_t *sec);

/* Receive a message from zeromq socket.
 * Returns message on success, NULL on failure with errno set.
 */
flux_msg_t *flux_msg_recvzsock (void *dest);
flux_msg_t *flux_msg_recvzsock_munge (void *sock, flux_sec_t *sec);

/* Initialize iobuf members.
 */
void flux_msg_iobuf_init (struct flux_msg_iobuf *iobuf);

/* Free any internal memory allocated to iobuf.
 * Only necessary if destroying with partial I/O in progress.
 */
void flux_msg_iobuf_clean (struct flux_msg_iobuf *iobuf);

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
 * Flags can be 0 or FLUX_MSGFLAG_JSON (hint for decoding).
 */
int flux_msg_get_payload (const flux_msg_t *msg, int *flags,
                          const void **buf, int *size);
int flux_msg_set_payload (flux_msg_t *msg, int flags,
                          const void *buf, int size);
bool flux_msg_has_payload (const flux_msg_t *msg);

/* Get/set JSON payload.
 * flux_msg_set_json() accepts a NULL json_str (no payload).
 * flux_msg_get_json() will set json_str to NULL if there is no payload
 * pack/unpack functions use jansson pack/unpack style arguments for
 * encoding/decoding the JSON object payload directly from/to its members.
 */
int flux_msg_set_json (flux_msg_t *msg, const char *json_str);
int flux_msg_pack (flux_msg_t *msg, const char *fmt, ...);
int flux_msg_vpack (flux_msg_t *msg, const char *fmt, va_list ap);

int flux_msg_get_json (const flux_msg_t *msg, const char **json_str);
int flux_msg_unpack (const flux_msg_t *msg, const char *fmt, ...);
int flux_msg_vunpack (const flux_msg_t *msg, const char *fmt, va_list ap);

/* Get/set nodeid (request only)
 * If flags includes FLUX_MSGFLAG_UPSTREAM, nodeid is the sending rank.
 * FLUX_NODEID_UPSTREAM is a stand in for this flag + sending rank in
 * higher level functions (not to be used here).
 */
enum {
    FLUX_NODEID_ANY      = 0xFFFFFFFF, //(~(uint32_t)0),
    FLUX_NODEID_UPSTREAM = 0xFFFFFFFE  //(~(uint32_t)1)
};
int flux_msg_set_nodeid (flux_msg_t *msg, uint32_t nodeid, int flags);
int flux_msg_get_nodeid (const flux_msg_t *msg, uint32_t *nodeid, int *flags);

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
    FLUX_ROLE_ALL = 0xFFFFFFFF,
};
int flux_msg_set_rolemask (flux_msg_t *msg, uint32_t rolemask);
int flux_msg_get_rolemask (const flux_msg_t *msg, uint32_t *rolemask);

/* Get/set errnum (response/keepalive only)
 */
int flux_msg_set_errnum (flux_msg_t *msg, int errnum);
int flux_msg_get_errnum (const flux_msg_t *msg, int *errnum);

/* Get/set sequence number (event only)
 */
int flux_msg_set_seq (flux_msg_t *msg, uint32_t seq);
int flux_msg_get_seq (const flux_msg_t *msg, uint32_t *seq);

/* Get/set status (keepalive only)
 */
int flux_msg_set_status (flux_msg_t *msg, int status);
int flux_msg_get_status (const flux_msg_t *msg, int *status);

/* Get/set/compare match tag (request/response only)
 */
enum {
    FLUX_MATCHTAG_NONE = 0,
    FLUX_MATCHTAG_GROUP_SHIFT = 20,
    FLUX_MATCHTAG_GROUP_MASK = 0xfff00000
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

/* Convert a numeric FLUX_MSGTYPE value to string,
 * or "unknown" if unrecognized.
 */
const char *flux_msg_typestr (int type);

/* NOTE: routing frames are pushed on a message traveling dealer
 * to router, and popped off a message traveling router to dealer.
 * A message intended for dealer-router sockets must first be enabled for
 * routing.
 */

/* Prepare a message for routing, which consists of pushing a nil delimiter
 * frame and setting FLUX_MSGFLAG_ROUTE.  This function is a no-op if the
 * flag is already set.
 * Returns 0 on success, -1 with errno set on failure.
 */
int flux_msg_enable_route (flux_msg_t *msg);

/* Strip route frames, nil delimiter, and clear FLUX_MSGFLAG_ROUTE flag.
 * This function is a no-op if the flag is already clear.
 * Returns 0 on success, -1 with errno set on failure.
 */
int flux_msg_clear_route (flux_msg_t *msg);

/* Push a route frame onto the message (mimic what dealer socket does).
 * 'id' is copied internally.
 * Returns 0 on success, -1 with errno set (e.g. EINVAL) on failure.
 */
int flux_msg_push_route (flux_msg_t *msg, const char *id);

/* Pop a route frame off the message and return identity (or NULL) in 'id'.
 * Caller must free 'id'.
 * Returns 0 on success, -1 with errno set (e.g. EPROTO) on failure.
 */
int flux_msg_pop_route (flux_msg_t *msg, char **id);

/* Copy the first routing frame (closest to delimiter) contents (or NULL)
 * to 'id'.  Caller must free 'id'.
 * For requests, this is the sender; for responses, this is the recipient.
 * Returns 0 on success, -1 with errno set (e.g. EPROTO) on failure.
 */
int flux_msg_get_route_first (const flux_msg_t *msg, char **id); /* closest to delim */

/* Copy the last routing frame (farthest from delimiter) contents (or NULL)
 * to 'id'.  Caller must free 'id'.
 * For requests, this is the last hop; for responses: this is the next hop.
 * Returns 0 on success, -1 with errno set (e.g. EPROTO) on failure.
 */
int flux_msg_get_route_last (const flux_msg_t *msg, char **id); /* farthest from delim */

/* Return the number of route frames in the message.
 * It is an EPROTO error if there is no route stack.
 * Returns 0 on success, -1 with errno set (e.g. EPROTO) on failure.
 */
int flux_msg_get_route_count (const flux_msg_t *msg);

/* Return a string representing the route stack in message.
 * Return NULL if there is no route delimiter; empty string if
 * the route stack contains no route frames).
 * Caller must free the returned string.
 */
char *flux_msg_get_route_string (const flux_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_MESSAGE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */


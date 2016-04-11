#ifndef _FLUX_CORE_MESSAGE_H
#define _FLUX_CORE_MESSAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct _zmsg_t flux_msg_t;

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
};

struct flux_match {
    int typemask;           /* bitmask of matching message types (or 0) */
    uint32_t matchtag;      /* matchtag block begin (or FLUX_MATCHTAG_NONE) */
    int bsize;              /* size of matchtag block (or 0) */
    char *topic_glob;       /* glob matching topic string (or NULL) */
};

#define FLUX_MATCH_ANY (struct flux_match){ \
    .typemask = FLUX_MSGTYPE_ANY, \
    .matchtag = FLUX_MATCHTAG_NONE, \
    .bsize = 0, \
    .topic_glob = NULL, \
}
#define FLUX_MATCH_EVENT (struct flux_match){ \
    .typemask = FLUX_MSGTYPE_EVENT, \
    .matchtag = FLUX_MATCHTAG_NONE, \
    .bsize = 0, \
    .topic_glob = NULL, \
}
#define FLUX_MATCH_REQUEST (struct flux_match){ \
    .typemask = FLUX_MSGTYPE_REQUEST, \
    .matchtag = FLUX_MATCHTAG_NONE, \
    .bsize = 0, \
    .topic_glob = NULL, \
}
#define FLUX_MATCH_RESPONSE (struct flux_match){ \
    .typemask = FLUX_MSGTYPE_RESPONSE, \
    .matchtag = FLUX_MATCHTAG_NONE, \
    .bsize = 0, \
    .topic_glob = NULL, \
}

struct flux_msg_iobuf {
    uint32_t nsize;
    size_t nsize_done;
    void *buf;
    size_t size;
    size_t done;
};

/* Create a new Flux message.
 * Returns new message or null on failure, with errno set (e.g. ENOMEM, EINVAL)
 * Caller must destroy message with flux_msg_destroy() or equivalent.
 */
flux_msg_t *flux_msg_create (int type);
void flux_msg_destroy (flux_msg_t *msg);

/* Duplicate msg, omitting payload if 'payload' is false.
 */
flux_msg_t *flux_msg_copy (const flux_msg_t *msg, bool payload);

/* Encode a flux_msg_t to buffer.
 * Returns 0 on success, -1 on failure with errno set.
 * Caller must free buf.
 */
int flux_msg_encode (const flux_msg_t *msg, void *buf, size_t *size);

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

/* Receive a message from zeromq socket.
 * Returns message on success, NULL on failure with errno set.
 */
flux_msg_t *flux_msg_recvzsock (void *dest);

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

/* Get/set/compare message topic string.
 * set adds/deletes/replaces topic frame as needed.
 */
int flux_msg_set_topic (flux_msg_t *msg, const char *topic);
int flux_msg_get_topic (const flux_msg_t *msg, const char **topic);

/* Get/set payload.
 * Set function adds/deletes/replaces payload frame as needed.
 * The new payload will be copied (caller retains ownership).
 * Any old payload is deleted.
 * Get_payload returns pointer to msg-owned buf.
 * Flags can be 0 or FLUX_MSGFLAG_JSON (hint for decoding).
 */
int flux_msg_set_payload (flux_msg_t *msg, int flags,
                          const void *buf, int size);
int flux_msg_get_payload (const flux_msg_t *msg, int *flags, void *buf, int *size);
bool flux_msg_has_payload (const flux_msg_t *msg);

/* Get/set json string payload.
 * set allows json_str to be NULL
 * get will set *json_str to NULL and return success if there is no payload.
 */
int flux_msg_set_payload_json (flux_msg_t *msg, const char *json_str);
int flux_msg_get_payload_json (const flux_msg_t *msg, const char **json_str);

/* Get/set nodeid (request only)
 * If flags includes FLUX_MSGFLAG_UPSTREAM, nodeid is the sending rank.
 * FLUX_NODEID_UPSTREAM is a stand in for this flag + sending rank in
 * higher level functions (not to be used here).
 */
#define FLUX_NODEID_ANY         (~(uint32_t)0)
#define FLUX_NODEID_UPSTREAM	(~(uint32_t)1)
int flux_msg_set_nodeid (flux_msg_t *msg, uint32_t nodeid, int flags);
int flux_msg_get_nodeid (const flux_msg_t *msg, uint32_t *nodeid, int *flags);

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
#define FLUX_MATCHTAG_NONE (0)
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

/* Return true if route stack contains a frame matching 's'
 */
bool flux_msg_has_route (const flux_msg_t *msg, const char *s);

#endif /* !_FLUX_CORE_MESSAGE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */


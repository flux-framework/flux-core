#ifndef _FLUX_CORE_MESSAGE_H
#define _FLUX_CORE_MESSAGE_H

#include <json.h>
#include <stdbool.h>
#include <czmq.h>
#include <stdint.h>

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

/* Create a new Flux message.
 * Returns new message or null on failure, with errno set (e.g. ENOMEM, EINVAL)
 * Caller must destroy message with zmsg_destroy() or equivalent.
 */
zmsg_t *flux_msg_create (int type);

/* Get/set/compare message topic string.
 * set adds/deletes/replaces topic frame as needed.
 * streq returns true if message topic string and 'topic' are identical.
 * strneq is the same, except only the first n chars of 'topic' are compared.
 */
int flux_msg_set_topic (zmsg_t *zmsg, const char *topic);
int flux_msg_get_topic (zmsg_t *zmsg, char **topic);
bool flux_msg_streq_topic (zmsg_t *zmsg, const char *topic);
bool flux_msg_strneq_topic (zmsg_t *zmsg, const char *topic, size_t n);

/* Get/set payload.
 * Set function adds/deletes/replaces payload frame as needed.
 * The new payload will be copied (caller retains ownership).
 * Any old payload is deleted.
 * Get_payload returns pointer to zmsg-owned buf.
 * Flags can be 0 or FLUX_MSGFLAG_JSON (hint for decoding).
 */
int flux_msg_set_payload (zmsg_t *zmsg, int flags, void *buf, int size);
int flux_msg_get_payload (zmsg_t *zmsg, int *flags, void **buf, int *size);
bool flux_msg_has_payload (zmsg_t *zmsg);

/* Get/set json payload.
 * set allows o to be NULL
 * get will set *o to NULL and return success if there is no payload.
 */
int flux_msg_set_payload_json (zmsg_t *zmsg, json_object *o);
int flux_msg_get_payload_json (zmsg_t *zmsg, json_object **o);

/* Get/set nodeid (request only)
 * If flags includes FLUX_NODEID_UPSTREAM, nodeid is the sending rank.
 * FLUX_NODEID_UPSTREAM is a stand in for this flag + sending rank in
 * higher level functions (not to be used here).
 */
#define FLUX_NODEID_ANY         (~(uint32_t)0)
#define FLUX_NODEID_UPSTREAM	(~(uint32_t)1)
int flux_msg_set_nodeid (zmsg_t *zmsg, uint32_t nodeid, int flags);
int flux_msg_get_nodeid (zmsg_t *zmsg, uint32_t *nodeid, int *flags);

/* Get/set errnum (response only)
 */
int flux_msg_set_errnum (zmsg_t *zmsg, int errnum);
int flux_msg_get_errnum (zmsg_t *zmsg, int *errnum);

/* Get/set sequence number (event only)
 */
int flux_msg_set_seq (zmsg_t *zmsg, uint32_t seq);
int flux_msg_get_seq (zmsg_t *zmsg, uint32_t *seq);

/* Get/set/compare match tag (request/response only)
 */
#define FLUX_MATCHTAG_NONE (0)
int flux_msg_set_matchtag (zmsg_t *zmsg, uint32_t matchtag);
int flux_msg_get_matchtag (zmsg_t *zmsg, uint32_t *matchtag);
bool flux_msg_cmp_matchtag (zmsg_t *zmsg, uint32_t matchtag);

/* Match a message.
 */
typedef struct {
    int typemask;           /* bitmask of matching message types (or 0) */
    uint32_t matchtag;      /* matchtag block begin (or FLUX_MATCHTAG_NONE) */
    int bsize;              /* size of matchtag block (or 0) */
    char *topic_glob;       /* glob matching topic string (or NULL) */
} flux_match_t;

bool flux_msg_cmp (zmsg_t *zmsg, flux_match_t match);

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
int flux_msg_enable_route (zmsg_t *zmsg);

/* Strip route frames, nil delimiter, and clear FLUX_MSGFLAG_ROUTE flag.
 * This function is a no-op if the flag is already clear.
 * Returns 0 on success, -1 with errno set on failure.
 */
int flux_msg_clear_route (zmsg_t *zmsg);

/* Push a route frame onto the message (mimic what dealer socket does).
 * 'id' is copied internally.
 * Returns 0 on success, -1 with errno set (e.g. EINVAL) on failure.
 */
int flux_msg_push_route (zmsg_t *zmsg, const char *id);

/* Pop a route frame off the message and return identity (or NULL) in 'id'.
 * Caller must free 'id'.
 * Returns 0 on success, -1 with errno set (e.g. EPROTO) on failure.
 */
int flux_msg_pop_route (zmsg_t *zmsg, char **id);

/* Copy the first routing frame (closest to delimiter) contents (or NULL)
 * to 'id'.  Caller must free 'id'.
 * For requests, this is the sender; for responses, this is the recipient.
 * Returns 0 on success, -1 with errno set (e.g. EPROTO) on failure.
 */
int flux_msg_get_route_first (zmsg_t *zmsg, char **id); /* closest to delim */

/* Copy the last routing frame (farthest from delimiter) contents (or NULL)
 * to 'id'.  Caller must free 'id'.
 * For requests, this is the last hop; for responses: this is the next hop.
 * Returns 0 on success, -1 with errno set (e.g. EPROTO) on failure.
 */
int flux_msg_get_route_last (zmsg_t *zmsg, char **id); /* farthest from delim */

/* Return the number of route frames in the message.
 * Returns 0 on success, -1 with errno set (e.g. EPROTO) on failure.
 */
int flux_msg_get_route_count (zmsg_t *zmsg);

/* Get/set message type
 * For FLUX_MSGTYPE_REQUEST: set_type initializes nodeid to FLUX_NODEID_ANY
 * For FLUX_MSGTYPE_RESPONSE: set_type initializes errnum to 0
 */
int flux_msg_set_type (zmsg_t *zmsg, int type);
int flux_msg_get_type (zmsg_t *zmsg, int *type);

/* Return string representation of message type.  Do not free.
 */
const char *flux_msgtype_string (int typemask);
const char *flux_msgtype_shortstr (int typemask);

#endif /* !_FLUX_CORE_MESSAGE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */


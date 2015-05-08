#ifndef _FLUX_CORE_MESSAGE_H
#define _FLUX_CORE_MESSAGE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct flux_msg_struct *flux_msg_t;

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

/* Create/destroy a new Flux message.
 * Create returns new message or NULL on failure, with errno set.
 */
flux_msg_t flux_msg_create (int type);
void flux_msg_destroy (flux_msg_t msg);

/* Get/set message type
 * For FLUX_MSGTYPE_REQUEST: set_type initializes nodeid to FLUX_NODEID_ANY
 * For FLUX_MSGTYPE_RESPONSE: set_type initializes errnum to 0
 */
int flux_msg_set_type (flux_msg_t msg, int type);
int flux_msg_get_type (flux_msg_t msg, int *type);

/* Get/set message topic string.
 * set adds/deletes/replaces topic frame as needed.
 * get returns pointer to msg-owned string.
 */
int flux_msg_set_topic (flux_msg_t msg, const char *topic);
int flux_msg_get_topic (flux_msg_t msg, const char **topic);

/* Get message flags.
 */
int flux_msg_get_flags (flux_msg_t msg, int *flags);

/* Get/set payload.
 * Set function adds/deletes/replaces payload frame as needed.
 * The new payload will be copied (caller retains ownership).
 * Any old payload is deleted.
 * Get_payload returns pointer to msg-owned buf.
 * Flags can be 0 or FLUX_MSGFLAG_JSON (hint for decoding).
 */
int flux_msg_set_payload (flux_msg_t msg, int flags, void *buf, int size);
int flux_msg_get_payload (flux_msg_t msg, int *flags, void **buf, int *size);

/* Get/set json payload (payload may be NULL).
 */
int flux_msg_set_payload_json (flux_msg_t msg, const char *json);
int flux_msg_get_payload_json (flux_msg_t msg, const char **json);

/* Get/set nodeid (request only)
 * If flags includes FLUX_NODEID_UPSTREAM, nodeid is the sending rank.
 * FLUX_NODEID_UPSTREAM is a stand in for this flag + sending rank in
 * higher level functions (not to be used here).
 */
#define FLUX_NODEID_ANY         (~(uint32_t)0)
#define FLUX_NODEID_UPSTREAM	(~(uint32_t)1)
int flux_msg_set_nodeid (flux_msg_t msg, uint32_t nodeid, int flags);
int flux_msg_get_nodeid (flux_msg_t msg, uint32_t *nodeid, int *flags);

/* Get/set errnum (response only)
 */
int flux_msg_set_errnum (flux_msg_t msg, int errnum);
int flux_msg_get_errnum (flux_msg_t msg, int *errnum);

/* Get/set sequence number (event only)
 */
int flux_msg_set_seq (flux_msg_t msg, uint32_t seq);
int flux_msg_get_seq (flux_msg_t msg, uint32_t *seq);

/* Get/set match tag (request/response only)
 */
#define FLUX_MATCHTAG_NONE (0)
int flux_msg_set_matchtag (flux_msg_t msg, uint32_t matchtag);
int flux_msg_get_matchtag (flux_msg_t msg, uint32_t *matchtag);

/* Get sender uuid (request only)
 */
int flux_msg_get_sender (flux_msg_t msg, const char **id);

/* Return string representation of message type.  Do not free.
 */
const char *flux_msgtype_string (int typemask);
const char *flux_msgtype_shortstr (int typemask);

#endif /* !_FLUX_CORE_MESSAGE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */


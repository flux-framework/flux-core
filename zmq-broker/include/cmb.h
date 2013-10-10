#ifndef HAVE_CMB_H
#define HAVE_CMB_H

#define CMB_FLAGS_TRACE         0x0001

#include <czmq.h>
#include "cmb_socket.h"

typedef struct cmb_struct *cmb_t;

typedef struct kvsdir_struct *kvsdir_t;
typedef struct kvsdir_iterator_struct *kvsitr_t;

/* Create/destroy cmb context used in the other calls.
 */
cmb_t cmb_init (void);
cmb_t cmb_init_full (const char *path, int flags);
void cmb_fini (cmb_t c);

/*  Send a json encoded message [o] via the current cmb handle [c]
 *      with tag [fmt]
 */
int cmb_send_message (cmb_t c, json_object *o, const char *fmt, ...);

/*  Initiate a recv on the current cmb handle [c].
 *   Returns by value a new json object [op].
 */
int cmb_recv_message (cmb_t c, char **tagp, json_object **op, int nb);

/*  Receive raw zmsg from cmb handle [c]
 */
zmsg_t *cmb_recv_zmsg (cmb_t c, int nb);

/* Ping a particular plugin.
 */
int cmb_ping (cmb_t c, char *tag, int seq, int padding, char **tagp,
              char **routep);

/* Request statistics for a particular plugin.
 * Returns JSON string, caller must free.
 */
char *cmb_stats (cmb_t c, char *name);

/* Watch traffic on the cmb sockets.
 * Packets are converted to ascii and printed on stderr.
 */
int cmb_snoop (cmb_t c, bool enable);
int cmb_snoop_one (cmb_t c);

/* Subscribe, send, and receive events.
 * Events are strings that begin with "event.".
 * Subscriptions are substrings, e.g. the subscription "event.live"
 * matches "event.live.up" and event.live.down".
 */
int cmb_event_subscribe (cmb_t c, char *subscription);
int cmb_event_unsubscribe (cmb_t c, char *subscription);
char *cmb_event_recv (cmb_t c);
int cmb_event_send (cmb_t c, char *event);

/* Execute the named barrier across the session.
 * The barrier can be any size.
 */
int cmb_barrier (cmb_t c, const char *name, int nprocs);

/* Get/put key-value pairs.
 */
enum {
    KVS_GET_WATCH = 1,      /* start watch */
    KVS_GET_NEXT = 2,       /* receive next watch change */
};

int cmb_kvs_get (cmb_t c, const char *key, json_object **valp, int flags);
int cmb_kvs_get_dir (cmb_t c, const char *key, kvsdir_t *dirp, int flags);
int cmb_kvs_get_string (cmb_t c, const char *key, char **valp, int flags);
int cmb_kvs_get_int (cmb_t c, const char *key, int *valp, int flags);
int cmb_kvs_get_int64 (cmb_t c, const char *key, int64_t *valp, int flags);
int cmb_kvs_get_double (cmb_t c, const char *key, double *valp, int flags);
int cmb_kvs_get_boolean (cmb_t c, const char *key, bool *valp, int flags);

int cmb_kvs_get_at (kvsdir_t dir, const char *name, json_object **valp);
int cmb_kvs_get_dir_at (kvsdir_t dir, const char *name, kvsdir_t *dirp);
int cmb_kvs_get_string_at (kvsdir_t dir, const char *name, char **valp);
int cmb_kvs_get_int_at (kvsdir_t dir, const char *name, int *valp);
int cmb_kvs_get_int64_at (kvsdir_t dir, const char *name, int64_t *valp);
int cmb_kvs_get_double_at (kvsdir_t dir, const char *name, double *valp);
int cmb_kvs_get_boolean_at (kvsdir_t dir, const char *name, bool *valp);

void cmb_kvsdir_destroy (kvsdir_t dir);
const char *cmb_kvsdir_key (kvsdir_t dir);
char *cmb_kvsdir_key_at (kvsdir_t dir, const char *name);

kvsitr_t cmb_kvsitr_create (kvsdir_t dir);
void cmb_kvsitr_destroy (kvsitr_t itr);
const char *cmb_kvsitr_next (kvsitr_t itr);

bool cmb_kvsdir_exists (kvsdir_t dir, const char *name);
bool cmb_kvsdir_isdir (kvsdir_t dir, const char *name);
bool cmb_kvsdir_isstring (kvsdir_t dir, const char *name);
bool cmb_kvsdir_isint (kvsdir_t dir, const char *name);
bool cmb_kvsdir_isint64 (kvsdir_t dir, const char *name);
bool cmb_kvsdir_isdouble (kvsdir_t dir, const char *name);
bool cmb_kvsdir_isboolean (kvsdir_t dir, const char *name);

int cmb_kvs_put (cmb_t c, const char *key, json_object *val);
int cmb_kvs_put_string (cmb_t c, const char *key, const char *val);
int cmb_kvs_put_int (cmb_t c, const char *key, int val);
int cmb_kvs_put_int64 (cmb_t c, const char *key, int64_t val);
int cmb_kvs_put_double (cmb_t c, const char *key, double val);
int cmb_kvs_put_boolean (cmb_t c, const char *key, bool val);

int cmb_kvs_unlink (cmb_t c, const char *key);
int cmb_kvs_mkdir (cmb_t c, const char *key);

int cmb_kvs_commit (cmb_t c);
int cmb_kvs_fence (cmb_t c, const char *name, int nprocs);

int cmb_kvs_dropcache (cmb_t c);

/* Log messages.
 * 'fac' is like syslog facility, only an arbitrary string.
 * It is suggested to use pub-sub topic string form.
 * If 'src' is null, it will be the cmb rank (node number).
 * 'lev' is syslog level.
 */
void cmb_log_set_facility (cmb_t c, const char *facility);
int cmb_vlog (cmb_t c, int lev, const char *fmt, va_list ap);
int cmb_log (cmb_t c, int lev, const char *fmt, ...)
            __attribute__ ((format (printf, 3, 4)));

/* Read the logs.
 * Call subscribe/unsubscribe multiple times to maintain subscription list,
 * then call cmb_log_recv to receive each subscribed-to message.
 * Call dump(), then recv()'s to get contents of circular buffer, terminated
 * by errnum=0 response.
 */
int cmb_log_subscribe (cmb_t c, int lev, const char *sub);
int cmb_log_unsubscribe (cmb_t c, const char *sub);
int cmb_log_dump (cmb_t c, int lev, const char *fac);
char *cmb_log_recv (cmb_t c, int *lp, char **fp, int *cp,
                    struct timeval *tvp, char **sp);

/* Manipulate (local) cmb routing tables.
 * Add and del are fire and forget (no reply).
 * Query returns JSON string, caller must free.
 */
int cmb_route_add (cmb_t c, char *dst, char *gw);
int cmb_route_del (cmb_t c, char *dst, char *gw);
char *cmb_route_query (cmb_t c);

/* Return cmb rank and size.
 */
int cmb_rank (cmb_t c);
int cmb_size (cmb_t c);

#endif /* !HAVE_CMB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

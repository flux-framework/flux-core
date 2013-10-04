#ifndef HAVE_CMB_H
#define HAVE_CMB_H

#define CMB_API_PATH_TMPL       "/tmp/cmb_socket.uid%d"

#define CMB_FLAGS_TRACE         0x0001

#include <czmq.h>

typedef struct cmb_struct *cmb_t;

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

/* Note on kvs.watch:
 * To "watch" a value, first call cmb_kvs_get[_*] with flags == KVS_GET_WATCH,
 * which always returns a value.  After this, whenever the requested key
 * changes, a reply is returned asynchronously which can be fetched with
 * cmb_kvs_get[_*] with flags == KVS_GET_NEXT.  Watching should be done
 * in a dedicated thread with its own cmb_t context as the async kvs.get.watch
 * replies would interfere with other RPC's.  The interface only supports
 * watching one value per context.
 */

/* Flags to cmb_kvs_get() series of functions.
 * You may 'or' them, for example KVS_GET_WATCH | KVS_GET_DIRECTORY.
 */
enum {
    KVS_GET_WATCH = 1,      /* start watch */
    KVS_GET_NEXT = 2,       /* receive next watch change */
    KVS_GET_DIRECTORY = 4,  /* get directory object */
    KVS_GET_DEEP = 8,       /* if directory object, get deep object */
};

/* Convenience functions for simple type values.
 */
/* N.B. get_string returns a copy of string; put_string makes a copy */
int cmb_kvs_get_string (cmb_t c, const char *key, char **valp, int flags);
int cmb_kvs_put_string (cmb_t c, const char *key, const char *val);
int cmb_kvs_get_int (cmb_t c, const char *key, int *valp, int flags);
int cmb_kvs_put_int (cmb_t c, const char *key, int val);
int cmb_kvs_get_int64 (cmb_t c, const char *key, int64_t *valp, int flags);
int cmb_kvs_put_int64 (cmb_t c, const char *key, int64_t val);
int cmb_kvs_get_double (cmb_t c, const char *key, double *valp, int flags);
int cmb_kvs_put_double (cmb_t c, const char *key, double val);
int cmb_kvs_get_boolean (cmb_t c, const char *key, bool *valp, int flags);
int cmb_kvs_put_boolean (cmb_t c, const char *key, bool val);

/* Get/put JSON values.
 * get: call json_object_put() on returned value object when done with it.
 * put: calls json_object_get() on passed in value object.
 */
int cmb_kvs_get (cmb_t c, const char *key, json_object **valp, int flags);
int cmb_kvs_put (cmb_t c, const char *key, json_object *val);

/* Singleton commit.  This ensures that any values just put by the calling
 * process will be available to a local get.  N.B. the commit affects the
 * entire session, but when the call returns one cannot be sure the commit
 * has been processed anywhere but the local node.  Use a cmb_barrier or 
 * cmb_kvs_fence if cross-session synchronization is needed.
 */
int cmb_kvs_commit (cmb_t c);

/* Collective commit. Internally it executes a barrier across 'nprocs'
 * tasks, then performs a single commit.  The 'name' should be globally
 * unique, e.g. derived from lwj id with a sequence number appended;
 * but for each collective invocation, the (name, nrprocs) tuple must
 * be the same across all nprocs tasks.
 */
int cmb_kvs_fence (cmb_t c, const char *name, int nprocs);

/* Drop cached KVS data.  On the root node, this is a heavy-weight operation
 * that creates a deep JSON copy of the namespace and recreates the SHA1
 * store from that.  On non-root nodes, the SHA1 store is summarily dropped.
 */
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

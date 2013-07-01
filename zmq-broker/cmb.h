#define CMB_API_PATH            "/tmp/cmb_socket"
#define CMB_API_BUFSIZE         32768
#define CMB_API_FD_BUFSIZE      (CMB_API_BUFSIZE - 1024)

typedef struct cmb_struct *cmb_t;

/* Create/destroy cmb context used in the other calls.
 */
cmb_t cmb_init (void);
cmb_t cmb_init_full (const char *path, int flags);
void cmb_fini (cmb_t c);

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
int cmb_barrier (cmb_t c, char *name, int nprocs);

/* Get/put key-value pairs.
 * Errors from puts are deferred until commit.
 */
int cmb_kvs_put (cmb_t c, const char *key, const char *val);
char *cmb_kvs_get (cmb_t c, const char *key);
int cmb_kvs_commit (cmb_t c, int *errcountp, int *putcountp);

/* Return state of all nodes in the session.
 * Caller must free the two returned arrays (up and down).
 * To only get one, set the other argument to NULL.
 */
int cmb_live_query (cmb_t c, int **up, int *ulp, int **dp, int *dlp, int *nnp);

/* This is not working right now.
 */
int cmb_fd_open (cmb_t c, char *wname, char **np);

/* Log messages.
 * Subscriptions are not implemented yet (all messages are returned).
 */
int cmb_vlog (cmb_t c, const char *tag, const char *src,
              const char *fmt, va_list ap);
int cmb_log (cmb_t c, const char *tag, const char *src, const char *fmt, ...)
    __attribute__ ((format (printf, 4, 5)));
int cmb_log_subscribe (cmb_t c, const char *sub);
int cmb_log_unsubscribe (cmb_t c, const char *sub);
char *cmb_log_recv (cmb_t c, char **tagp, struct timeval *tvp, char **fromp);

/* Manipulate (local) cmb routing tables.
 * Add and del are fire and forget (no reply).
 * Query returns JSON string, caller must free.
 */
int cmb_route_add (cmb_t c, char *dst, char *gw);
int cmb_route_del (cmb_t c, char *dst, char *gw);
char *cmb_route_query (cmb_t c);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

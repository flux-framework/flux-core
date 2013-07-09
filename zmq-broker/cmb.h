#define CMB_API_PATH            "/tmp/cmb_socket"
#define CMB_API_BUFSIZE         32768
#define CMB_API_FD_BUFSIZE      (CMB_API_BUFSIZE - 1024)

#define CMB_FLAGS_TRACE         0x0001

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

/* Get/put key-value config pairs.
 */
int cmb_conf_put (cmb_t c, const char *key, const char *val);
char *cmb_conf_get (cmb_t c, const char *key);
int cmb_conf_commit (cmb_t c);
int cmb_conf_list (cmb_t c);
int cmb_conf_next (cmb_t c, char **key, char **val);

/* Return state of all nodes in the session.
 * Caller must free the two returned arrays (up and down).
 * To only get one, set the other argument to NULL.
 */
int cmb_live_query (cmb_t c, int **up, int *ulp, int **dp, int *dlp, int *nnp);

/* Log messages.
 * 'fac' is like syslog facility, only an arbitrary string.
 * It is suggested to use pub-sub topic string form.
 * If 'src' is null, it will be the cmb rank (node number).
 * 'pri' is like syslog priority, use CMB_LOG values.
 */
typedef enum {
    CMB_LOG_EMERG=0,    /* system is unusable */
    CMB_LOG_ALERT=1,    /* action must be taken immediately */
    CMB_LOG_CRIT=2,     /* critical conditions */
    CMB_LOG_ERR=3,      /* error conditions */
    CMB_LOG_WARNING=4,  /* warning conditions */
    CMB_LOG_NOTICE=5,   /* normal, but significant, condition */
    CMB_LOG_INFO=6,     /* informational message */
    CMB_LOG_DEBUG=7,    /* debug level message */
} logpri_t;
void cmb_log_set_facility (cmb_t c, const char *facility);
int cmb_vlog (cmb_t c, logpri_t pri, const char *fmt, va_list ap);
int cmb_log (cmb_t c, logpri_t pri, const char *fmt, ...)
            __attribute__ ((format (printf, 3, 4)));

/* Read the logs.
 * Call subscribe/unsubscribe multiple times to maintain subscription list,
 * then call cmb_log_recv to receive each subscribed-to message.
 * Call dump(), then recv()'s to get contents of circular buffer, terminated
 * by errnum=0 response.
 */
int cmb_log_subscribe (cmb_t c, logpri_t pri, const char *sub);
int cmb_log_unsubscribe (cmb_t c, const char *sub);
int cmb_log_dump (cmb_t c, logpri_t pri, const char *fac);
char *cmb_log_recv (cmb_t c, logpri_t *pp, char **fp, int *cp,
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

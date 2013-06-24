typedef struct {
    char *treeout_uri;  /* upreq_out connects to this port */
    char *treeout_uri2; /* dnreq_in connects to this port */
    int rank;
} parent_t;

/* server->route hashes to route_t structures.
 */
enum {
    ROUTE_FLAGS_PRIVATE = 1,
};
typedef struct {
    char *gw;
    int flags;
} route_t;

/* Config state.
 * This is static and can be shared by all threads.
 */
typedef struct {
    char *treein_uri;   /* upreq_in binds to this port and UPREQ_URI */
    char *treein_uri2;  /* dnreq_out binds to this port and DNREQ_URI */
    char *event_in_uri;
    char *event_out_uri;
    bool verbose;
    char rankstr[16];
    int rank;
    int size;
    char *plugins;
    parent_t parent[2];
    int parent_len;
    /* Options set in cmbd getopt and read by plugins.
     * FIXME: need a spank-like abstraction for plugin options.
     */
    int sync_period_msec;
    char *api_sockpath;
    int *live_children;
    int live_children_len;
    char *kvs_redis_server;
} conf_t;

/* Server state.
 * This is dynamic and is only accessed by the main (zpoll) thread.
 */
typedef struct {
    zctx_t *zctx;
    void *zs_eventout;
    void *zs_eventin;
    void *zs_upreq_out;
    void *zs_dnreq_in;
    void *zs_upreq_in;
    void *zs_dnreq_out;
    void *zs_snoop;
    void *zs_plout_event;
    void *zs_plin_event;
    int parent_cur;
    zhash_t *route;
    zhash_t *plugins;
} server_t;

#define UPREQ_URI           "inproc://upreq"
#define DNREQ_URI           "inproc://dnreq"

#define PLOUT_EVENT_URI     "inproc://plout_event"
#define PLIN_EVENT_URI      "inproc://plin_event"

#define SNOOP_URI           "inproc://snoop"


/* plugin.c calls these (in the cmbd thread) to manage routes to
 * plugins at plugin init/finalize time
 */
int cmb_route_add_internal (server_t *srv, const char *dst, const char *gw,
                            int flags);
void cmb_route_del_internal (server_t *srv, const char *dst, const char *gw);


#define MAX_PARENTS 2
typedef struct {
    char *upreq_uri;
    char *dnreq_uri;
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
    char *upreq_in_uri;
    char *dnreq_out_uri;
    char *upev_in_uri;
    char *upev_out_uri;
    char *dnev_in_uri;
    char *dnev_out_uri;
    bool verbose;
    char rankstr[16];
    int rank;
    int size;
    char *plugins;
    parent_t parent[MAX_PARENTS];
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
    void *zs_upreq_out;
    void *zs_dnreq_in;
    void *zs_upreq_in;
    void *zs_dnreq_out;
    void *zs_snoop;
    void *zs_upev_out;
    void *zs_upev_in;
    void *zs_dnev_out;
    void *zs_dnev_in;
    int parent_cur;
    bool parent_alive[MAX_PARENTS];
    zhash_t *route;
    zhash_t *plugins;
} server_t;

#define UPREQ_URI           "inproc://upreq"
#define DNREQ_URI           "inproc://dnreq"

#define DNEV_OUT_URI        "inproc://evout"
#define DNEV_IN_URI         "inproc://evin"

#define SNOOP_URI           "inproc://snoop"


/* plugin.c calls these (in the cmbd thread) to manage routes to
 * plugins at plugin init/finalize time
 */
int cmb_route_add_internal (server_t *srv, const char *dst, const char *gw,
                            int flags);
void cmb_route_del_internal (server_t *srv, const char *dst, const char *gw);


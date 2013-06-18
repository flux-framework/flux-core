typedef struct {
    char *treeout_uri;
    int rank;
} parent_t;

typedef struct {
    int gw;
} route_t;

typedef struct {
    char *treein_uri;
    parent_t parent[2];
    int parent_len;
    int *children;
    int children_len;
    char *event_in_uri;
    char *event_out_uri;
    bool verbose;
    int syncperiod_msec;
    char *redis_server;
    char *apisockpath;
    int rank;
    int size;
    char *plugins;
} conf_t;


/* (dealer)              (dealer)
 +-----------------------------------------------------------------+
 | upreq_out             dnreq_in                                  |
 |                                                                 |
 |                                                                 |
 |                                                                 |
 |                                                                 |
 | upreq_in              dnreq_out                                 |
 +-----------------------------------------------------------------+
  (router)               (router)

*/

typedef struct {
    zctx_t *zctx;
    void *zs_eventout;
    void *zs_eventin;
    void *zs_upreq_out;
    void *zs_snoop;
    void *zs_plout_event;
    void *zs_plin;
    void *zs_upreq_in;
    void *zs_plin_event;
    void *zs_plin_tree;
    int parent_cur;
    zhash_t *route;
    zhash_t *plugins;
} server_t;

#define PLOUT_URI_TMPL      "inproc://plout_%s"
#define PLIN_URI            "inproc://plin"
#define ROUTER_URI          "inproc://router"
#define PLOUT_EVENT_URI     "inproc://plout_event"
#define PLIN_EVENT_URI      "inproc://plin_event"
#define SNOOP_URI           "inproc://snoop"

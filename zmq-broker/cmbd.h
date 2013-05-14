typedef struct {
    char *treeout_uri;
    int rank;
} parent_t;

typedef struct {
    char *treein_uri;
    parent_t parent[2];
    int parent_len;
    int *children;
    int children_len;
    char *event_uri;
    bool verbose;
    int syncperiod_msec;
    char *redis_server;
    char *apisockpath;
    int rank;
    int size;
} conf_t;

typedef struct {
    zctx_t *zctx;
    void *zs_eventout;
    void *zs_eventin;
    void *zs_upreq;
    void *zs_snoop;
    void *zs_plout_event;
    void *zs_plin;
    void *zs_router;
    void *zs_plin_event;
    void *zs_plin_tree;
    int parent_cur;
    zhash_t *plugins;
} server_t;

#define PLOUT_URI_TMPL      "inproc://plout_%s"
#define PLIN_URI            "inproc://plin"
#define ROUTER_URI          "inproc://router"
#define PLOUT_EVENT_URI     "inproc://plout_event"
#define PLIN_EVENT_URI      "inproc://plin_event"
#define SNOOP_URI           "inproc://snoop"

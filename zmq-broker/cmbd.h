typedef struct {
    char *treein_uri;
    char *treeout_uri;
    char *event_uri;
    bool verbose;
    int syncperiod_msec;
    char *redis_server;
    char *apisockpath;
    int rank;
    int size;
} conf_t;

typedef struct plugin_struct *plugin_t;

typedef struct {
    conf_t *conf;
    void *zs_in;
    void *zs_in_event;
    void *zs_out;
    void *zs_out_event;
    void *zs_out_tree;
    pthread_t t;
    plugin_t plugin;
    void *ctx;
} plugin_ctx_t;

struct plugin_struct {
    void (*pollFn)(plugin_ctx_t *p);
    void (*initFn)(plugin_ctx_t *p);
    void (*finiFn)(plugin_ctx_t *p);
};

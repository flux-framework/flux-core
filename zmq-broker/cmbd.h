typedef struct {
    char *treein_uri;
    char *treeout_uri;
    char *event_uri;
    char *plout_uri;
    char *plout_event_uri;
    char *plin_uri;
    char *plin_event_uri;
    char *plin_tree_uri;
    bool verbose;
    int syncperiod_msec;
    char *redis_server;
    char *apisockpath;
    int rank;
    int size;
} conf_t;

typedef void * (*plugin_poll_t)(void *arg);

typedef struct plugin_struct {
    conf_t *conf;
    void *zs_in;
    void *zs_in_event;
    void *zs_out;
    void *zs_out_event;
    void *zs_out_tree;
    pthread_t poll_thd;
    plugin_poll_t poll_fun;
    void *ctx;
} plugin_t;

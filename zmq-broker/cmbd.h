typedef struct {
    char *prog;
    char *treein_uri;
    char *treeout_uri;
    char *event_uri;
    char *plout_uri;
    char *plin_uri;
    char *plin_event_uri;
    char *plin_tree_uri;
    bool verbose;
    int syncperiod_msec;
    char *redis_server;
    int rank;
    int size;
} conf_t;

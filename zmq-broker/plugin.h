typedef struct plugin_struct *plugin_t;

typedef struct {
    conf_t *conf;
    void *zs_in;
    void *zs_in_event;
    void *zs_out;
    void *zs_out_event;
    void *zs_out_tree;
    long timeout;
    pthread_t t;
    plugin_t plugin;
    void *ctx;
} plugin_ctx_t;

struct plugin_struct {
    void (*timeoutFn)(plugin_ctx_t *p);
    void (*recvFn)(plugin_ctx_t *p, zmsg_t *msg);
    void (*pollFn)(plugin_ctx_t *p);
    void (*initFn)(plugin_ctx_t *p);
    void (*finiFn)(plugin_ctx_t *p);
};

void plugin_init (conf_t *conf, server_t *srv);
void plugin_fini (conf_t *conf, server_t *srv);

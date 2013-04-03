typedef struct {
    char *prog;
    char treein_uri[MAXHOSTNAMELEN];
    char treeout_uri[MAXHOSTNAMELEN];
    char eventout_uri[MAXHOSTNAMELEN];
    char eventin_uri[MAXHOSTNAMELEN];
    char plout_uri[MAXHOSTNAMELEN];
    char plin_uri[MAXHOSTNAMELEN];
    char plin_event_uri[MAXHOSTNAMELEN];
    char plin_tree_uri[MAXHOSTNAMELEN];
    bool root_server;
    bool leaf_server;
    int nnodes; /* from SLURM env */
    char *rootnode; /* from SLURM env */
    bool verbose;
    int syncperiod_msec;
} conf_t;

/* N.B. Decode functions return pointers to storage owned by the json object.
 */

// flags
enum {
    KVS_PROTO_READDIR = 1,      /* get, watch */
    KVS_PROTO_READLINK = 2,     /* get, watch */
    KVS_PROTO_ONCE = 4,         /* watch */
    KVS_PROTO_FIRST = 8,        /* watch */
    KVS_PROTO_TREEOBJ = 16,     /* get */
};

/* kvs.get
 */
json_object *kp_tget_enc (json_object *rootdir,
                          const char *key, int flags);
int kp_tget_dec (json_object *o, json_object **rootdir,
                 const char **key, int *flags);

json_object *kp_rget_enc (json_object *rootdir, json_object *val);
int kp_rget_dec (json_object *o, json_object **rootdir, json_object **val);


/* kvs.watch
 */
json_object *kp_twatch_enc (const char *key, json_object *val, int flags);
int kp_twatch_dec (json_object *o, const char **key, json_object **val,
                   int *flags);

json_object *kp_rwatch_enc (json_object *val);
int kp_rwatch_dec (json_object *o, json_object **val);

/* kvs.unwatch
 */
json_object *kp_tunwatch_enc (const char *key);
int kp_tunwatch_dec (json_object *o, const char **key);
/* unwatch response is just errnum */

/* kvs.fence
 * kvs.relayfence
 */
json_object *kp_tfence_enc (const char *name, int nprocs, int flags,
                            json_object *ops);
int kp_tfence_dec (json_object *o, const char **name, int *nprocs,
                   int *flags, json_object **ops);

/* kvs.getroot (request)
 */
/* empty request payload */
json_object *kp_rgetroot_enc (int rootseq, const char *rootdir);
int kp_rgetroot_dec (json_object *o, int *rootseq, const char **rootdir);

/* kvs.setroot (event)
 */
json_object *kp_tsetroot_enc (int rootseq, const char *rootdir,
                              json_object *root, json_object *names);
int kp_tsetroot_dec (json_object *o, int *rootseq, const char **rootdir,
                     json_object **root, json_object **names);

/* kvs.error (event)
 */
json_object *kp_terror_enc (json_object *names, int errnum);
int kp_terror_dec (json_object *o, json_object **names, int *errnum);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

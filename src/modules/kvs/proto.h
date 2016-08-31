/* N.B. Decode functions return pointers to storage owned by the json object.
 */

/* kvs.get
 */
json_object *kp_tget_enc (const char *key, bool dir, bool link);
int kp_tget_dec (json_object *o, const char **key, bool *dir, bool *link);

json_object *kp_rget_enc (const char *key, json_object *val);
int kp_rget_dec (json_object *o, json_object **val);


/* kvs.watch
 */
json_object *kp_twatch_enc (const char *key, json_object *val,
                            bool once, bool first, bool dir, bool link);
int kp_twatch_dec (json_object *o, const char **key, json_object **val,
                   bool *once, bool *first, bool *dir, bool *link);

json_object *kp_rwatch_enc (const char *key, json_object *val);
int kp_rwatch_dec (json_object *o, json_object **val);

/* kvs.unwatch
 */
json_object *kp_tunwatch_enc (const char *key);
int kp_tunwatch_dec (json_object *o, const char **key);
/* unwatch response is just errnum */

/* kvs.fence
 * kvs.relayfence
 */
json_object *kp_tfence_enc (const char *name, int nprocs, json_object *ops);
int kp_tfence_dec (json_object *o, const char **name, int *nprocs,
                   json_object **ops);

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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

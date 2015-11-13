/* N.B. Decode functions return pointers to storage owned by the json object.
 */

/* kvs.put
 */
json_object *kp_tput_enc (const char *key, const char *json_str,
                          bool link, bool dir);
int kp_tput_dec (json_object *o, const char **key, json_object **val,
                 bool *link, bool *dir);
/* put response is just errnum */


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

/* kvs.commit
 */
json_object *kp_tcommit_enc (const char *sender, json_object *dirents,
                             const char *fence, int nprocs);
int kp_tcommit_dec (json_object *o, const char **sender, json_object **dirents,
                    const char **fence, int *nprocs);

json_object *kp_rcommit_enc (int rootseq, const char *rootdir,
                             const char *sender);
int kp_rcommit_dec (json_object *o, int *rootseq, const char **rootdir,
                    const char **sender);

/* kvs.getroot
 */
/* empty request payload */
json_object *kp_rgetroot_enc (int rootseq, const char *rootdir);
int kp_rgetroot_dec (json_object *o, int *rootseq, const char **rootdir);

/* kvs.setroot (event)
 */
json_object *kp_tsetroot_enc (int rootseq, const char *rootdir,
                              json_object *root, const char *fence);
int kp_tsetroot_dec (json_object *o, int *rootseq, const char **rootdir,
                     json_object **root, const char **fence);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

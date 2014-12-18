/* N.B. Decode functions return pointers to storage owned by the json object,
 * except modctl_tload_dec() which returns argv that must be freed
 * (but its elements are owned by the json object and should not be freed).
 */

json_object *modctl_tunload_enc (const char *name);
int modctl_tunload_dec (json_object *o, const char **name);

json_object *modctl_runload_enc (int errnum);
int modctl_runload_dec (json_object *o, int *errnum);

json_object *modctl_tload_enc (const char *path, int argc, char **argv);
int modctl_tload_dec (json_object *o, const char **path,
                      int *argc, const char ***argv);

json_object *modctl_rload_enc (int errnum);
int modctl_rload_dec (json_object *o, int *errnum);

json_object *modctl_tlist_enc (const char *svc);
int modctl_tlist_dec (json_object *o, const char **svc);

json_object *modctl_rlist_enc (void);
int modctl_rlist_enc_add (json_object *o, const char *name, int size,
                          const char *digest, int idle);
int modctl_rlist_enc_errnum (json_object *o, int errnum);

int modctl_rlist_dec (json_object *o, int *errnum, int *len);
int modctl_rlist_dec_nth (json_object *o, int n, const char **name,
                                 int *size, const char **digest, int *idle);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

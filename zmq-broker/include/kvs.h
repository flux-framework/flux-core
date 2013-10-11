typedef struct kvsdir_struct  *kvsdir_t;
void kvsdir_destroy (kvsdir_t dir);

/* The basic get and put operations, with convenience functions
 * for simple types.  You will get an error if you call kvs_get()
 * on a directory (errno = EISDIR).  Use kvs_get_dir() which returns
 * the opaque kvsdir_t type.  kvs_get(), kvs_get_dir(), and kvs_get_string()
 * return values that must be freed with json_object_put(), kvsdir_destroy(),
 * and free() respectively.
 */
int kvs_get (void *h, const char *key, json_object **valp);
int kvs_get_dir (void *h, const char *key, kvsdir_t *dirp);
int kvs_get_string (void *h, const char *key, char **valp);
int kvs_get_int (void *h, const char *key, int *valp);
int kvs_get_int64 (void *h, const char *key, int64_t *valp);
int kvs_get_double (void *h, const char *key, double *valp);
int kvs_get_boolean (void *h, const char *key, bool *valp);

/* kvs_put() and kvs_put_string() both make copies of the value argument
 * The caller retains ownership of the original.
 */
int kvs_put (void *h, const char *key, json_object *val);
int kvs_put_string (void *h, const char *key, const char *val);
int kvs_put_int (void *h, const char *key, int val);
int kvs_put_int64 (void *h, const char *key, int64_t val);
int kvs_put_double (void *h, const char *key, double val);
int kvs_put_boolean (void *h, const char *key, bool val);

/* An iterator interface for walking the list of names in a kvsdir_t
 * returned by kvs_get_dir().
 */
typedef struct kvsdir_iterator_struct *kvsitr_t;
kvsitr_t kvsitr_create (kvsdir_t dir);
void kvsitr_destroy (kvsitr_t itr);
const char *kvsitr_next (kvsitr_t itr);

/* Test attributes of a name returned from kvsitr_next().
 */
bool kvsdir_exists (kvsdir_t dir, const char *name);
bool kvsdir_isdir (kvsdir_t dir, const char *name);
bool kvsdir_isstring (kvsdir_t dir, const char *name);
bool kvsdir_isint (kvsdir_t dir, const char *name);
bool kvsdir_isint64 (kvsdir_t dir, const char *name);
bool kvsdir_isdouble (kvsdir_t dir, const char *name);
bool kvsdir_isboolean (kvsdir_t dir, const char *name);

/* Get key associated with a directory or directory entry.
 * kvsdir_key() returns a string owned by the kvsdir_t, while kvsdir_key_at()
 * returns a dynamically allocated string that caller must free().
 */
const char *kvsdir_key (kvsdir_t dir);
char *kvsdir_key_at (kvsdir_t dir, const char *name);

/* Read the value of a name returned from kvsitr_next().
 */
int kvs_get_at (kvsdir_t dir, const char *name, json_object **valp);
int kvs_get_dir_at (kvsdir_t dir, const char *name, kvsdir_t *dirp);
int kvs_get_string_at (kvsdir_t dir, const char *name, char **valp);
int kvs_get_int_at (kvsdir_t dir, const char *name, int *valp);
int kvs_get_int64_at (kvsdir_t dir, const char *name, int64_t *valp);
int kvs_get_double_at (kvsdir_t dir, const char *name, double *valp);
int kvs_get_boolean_at (kvsdir_t dir, const char *name, bool *valp);

/* Remove a key from the namespace.  If it represents a directory,
 * its contents are also removed.
 */
int kvs_unlink (void *h, const char *key);

/* Create an empty directory.
 */
int kvs_mkdir (void *h, const char *key);

/* kvs_commit() must be called after kvs_put*, kvs_unlink, and kvs_mkdir
 * to finalize the update.  The new data is immediately available on
 * the calling node when the commit returns.
 */
int kvs_commit (void *h);

/* kvs_fence() is a collective commit operation.  nprocs tasks make the
 * call with identical arguments.  It is internally optimized to minimize
 * the work that needs to be done.  Once the call returns, all changes
 * from participating tasks are available to all tasks.
 */
int kvs_fence (void *h, const char *name, int nprocs);

/* Garbage collect the cache.  On the root node, drop all data that
 * doesn't have a reference in the namespace.  On other nodes, the entire
 * cache is dropped and will be reloaded on demand.
 */
int kvs_dropcache (void *h);

/* These are called internally to register functions that are used
 * by the kvs implementation.
 */
typedef json_object *(RequestFun(void *h, json_object *req, const char *fmt, ...));
typedef int (BarrierFun(void *h, const char *name, int nprocs));
void kvs_reqfun_set (RequestFun *fun);
void kvs_barrierfun_set (BarrierFun *fun);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

typedef struct kvsdir_struct  *kvsdir_t;
void kvsdir_destroy (kvsdir_t dir);

/* The basic get and put operations, with convenience functions
 * for simple types.  You will get an error if you call kvs_get()
 * on a directory (return -1, errno = EISDIR).  Use kvs_get_dir() which
 * returns the opaque kvsdir_t type.  kvs_get(), kvs_get_dir(), and
 * kvs_get_string() return values that must be freed with json_object_put(),
 * kvsdir_destroy(), and free() respectively.
 * These functions return -1 on error (errno set), 0 on success.
 */
int kvs_get (void *h, const char *key, json_object **valp);
int kvs_get_dir (void *h, const char *key, kvsdir_t *dirp);
int kvs_get_string (void *h, const char *key, char **valp);
int kvs_get_int (void *h, const char *key, int *valp);
int kvs_get_int64 (void *h, const char *key, int64_t *valp);
int kvs_get_double (void *h, const char *key, double *valp);
int kvs_get_boolean (void *h, const char *key, bool *valp);

/*
 * Convenience function to create kvsdir_t object from printf format
 * string. Returns NULL on failure with errno set.
 */
kvsdir_t kvsdir_create (void *h, const char *fmt, ...);

/* kvs_put() and kvs_put_string() both make copies of the value argument
 * The caller retains ownership of the original.
 * These functions return -1 on error (errno set), 0 on success.
 */
int kvs_put (void *h, const char *key, json_object *val);
int kvs_put_string (void *h, const char *key, const char *val);
int kvs_put_int (void *h, const char *key, int val);
int kvs_put_int64 (void *h, const char *key, int64_t val);
int kvs_put_double (void *h, const char *key, double val);
int kvs_put_boolean (void *h, const char *key, bool val);

/* kvsdir_put_* work as above but 'key' is relative to kvsdir object
 */
int kvsdir_put (kvsdir_t dir, const char *key, json_object *val);
int kvsdir_put_string (kvsdir_t dir, const char *key, const char *val);
int kvsdir_put_int (kvsdir_t dir, const char *key, int val);
int kvsdir_put_int64 (kvsdir_t dir, const char *key, int64_t val);
int kvsdir_put_double (kvsdir_t dir, const char *key, double val);
int kvsdir_put_boolean (kvsdir_t dir, const char *key, bool val);

/* An iterator interface for walking the list of names in a kvsdir_t
 * returned by kvs_get_dir().  kvsitr_create() always succeeds.
 * kvsitr_next() returns NULL when the last item is reached.
 */
typedef struct kvsdir_iterator_struct *kvsitr_t;
kvsitr_t kvsitr_create (kvsdir_t dir);
void kvsitr_destroy (kvsitr_t itr);
const char *kvsitr_next (kvsitr_t itr);
void kvsitr_rewind (kvsitr_t itr);

/* Test attributes of 'key', relative to kvsdir object.
 */
bool kvsdir_exists (kvsdir_t dir, const char *key);
bool kvsdir_isdir (kvsdir_t dir, const char *key);
bool kvsdir_isstring (kvsdir_t dir, const char *key);
bool kvsdir_isint (kvsdir_t dir, const char *key);
bool kvsdir_isint64 (kvsdir_t dir, const char *key);
bool kvsdir_isdouble (kvsdir_t dir, const char *key);
bool kvsdir_isboolean (kvsdir_t dir, const char *key);

/* Get key associated with a directory or directory entry.
 * Both functions always succeed.
 */
const char *kvsdir_key (kvsdir_t dir); /* caller does not free result */
char *kvsdir_key_at (kvsdir_t dir, const char *key); /* caller frees result */

/* Read the value of 'key', relative to kvsdir object.
 * These functions return -1 on error (errno set), 0 on success.
 */
int kvsdir_get (kvsdir_t dir, const char *key, json_object **valp);
int kvsdir_get_dir (kvsdir_t dir, const char *key, kvsdir_t *dirp);
int kvsdir_get_string (kvsdir_t dir, const char *key, char **valp);
int kvsdir_get_int (kvsdir_t dir, const char *key, int *valp);
int kvsdir_get_int64 (kvsdir_t dir, const char *key, int64_t *valp);
int kvsdir_get_double (kvsdir_t dir, const char *key, double *valp);
int kvsdir_get_boolean (kvsdir_t dir, const char *key, bool *valp);

/* Remove a key from the namespace.  If it represents a directory,
 * its contents are also removed.
 * Returns -1 on error (errno set), 0 on success.
 */
int kvs_unlink (void *h, const char *key);

/* Unlink relative to 'dir'
 */
int kvsdir_unlink (kvsdir_t dir, const char *key);

/* Create an empty directory.
 * Returns -1 on error (errno set), 0 on success.
 */
int kvs_mkdir (void *h, const char *key);

/* Create directory relative to 'dir'
 */
int kvsdir_mkdir (kvsdir_t dir, const char *key);


/* kvs_commit() must be called after kvs_put*, kvs_unlink, and kvs_mkdir
 * to finalize the update.  The new data is immediately available on
 * the calling node when the commit returns.
 * Returns -1 on error (errno set), 0 on success.
 */
int kvs_commit (void *h);

/* kvs_fence() is a collective commit operation.  nprocs tasks make the
 * call with identical arguments.  It is internally optimized to minimize
 * the work that needs to be done.  Once the call returns, all changes
 * from participating tasks are available to all tasks.
 * Returns -1 on error (errno set), 0 on success.
 */
int kvs_fence (void *h, const char *name, int nprocs);

/* Garbage collect the cache.  On the root node, drop all data that
 * doesn't have a reference in the namespace.  On other nodes, the entire
 * cache is dropped and will be reloaded on demand.
 * Returns -1 on error (errno set), 0 on success.
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

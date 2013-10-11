typedef struct kvsdir_struct  *kvsdir_t;
typedef struct kvsdir_iterator_struct *kvsitr_t;
typedef json_object *(RequestFun(void *h, json_object *req, const char *fmt, ...));
typedef int (BarrierFun(void *h, const char *name, int nprocs));

void kvs_reqfun_set (RequestFun *fun);
void kvs_barrierfun_set (BarrierFun *fun);

void kvsdir_destroy (kvsdir_t dir);
kvsdir_t kvsdir_create (void *h, const char *key, json_object *o);

const char *kvsdir_key (kvsdir_t dir);
void *kvsdir_handle (kvsdir_t dir);

kvsitr_t kvsitr_create (kvsdir_t dir);
void kvsitr_destroy (kvsitr_t itr);
const char *kvsitr_next (kvsitr_t itr);

char *kvsdir_key_at (kvsdir_t dir, const char *name);

int kvs_get (void *h, const char *key, json_object **valp);
int kvs_get_dir (void *h, const char *key, kvsdir_t *dirp);
int kvs_get_string (void *h, const char *key, char **valp);
int kvs_get_int (void *h, const char *key, int *valp);
int kvs_get_int64 (void *h, const char *key, int64_t *valp);
int kvs_get_double (void *h, const char *key, double *valp);
int kvs_get_boolean (void *h, const char *key, bool *valp);

bool kvsdir_exists (kvsdir_t dir, const char *name);
bool kvsdir_isdir (kvsdir_t dir, const char *name);
bool kvsdir_isstring (kvsdir_t dir, const char *name);
bool kvsdir_isint (kvsdir_t dir, const char *name);
bool kvsdir_isint64 (kvsdir_t dir, const char *name);
bool kvsdir_isdouble (kvsdir_t dir, const char *name);
bool kvsdir_isboolean (kvsdir_t dir, const char *name);

int kvs_get_at (kvsdir_t dir, const char *name, json_object **valp);
int kvs_get_dir_at (kvsdir_t dir, const char *name, kvsdir_t *dirp);
int kvs_get_string_at (kvsdir_t dir, const char *name, char **valp);
int kvs_get_int_at (kvsdir_t dir, const char *name, int *valp);
int kvs_get_int64_at (kvsdir_t dir, const char *name, int64_t *valp);
int kvs_get_double_at (kvsdir_t dir, const char *name, double *valp);
int kvs_get_boolean_at (kvsdir_t dir, const char *name, bool *valp);

int kvs_put (void *h, const char *key, json_object *val);
int kvs_put_string (void *h, const char *key, const char *val);
int kvs_put_int (void *h, const char *key, int val);
int kvs_put_int64 (void *h, const char *key, int64_t val);
int kvs_put_double (void *h, const char *key, double val);
int kvs_put_boolean (void *h, const char *key, bool val);

int kvs_unlink (void *h, const char *key);
int kvs_mkdir (void *h, const char *key);

int kvs_commit (void *h);
int kvs_fence (void *h, const char *name, int nprocs);

int kvs_dropcache (void *h);


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

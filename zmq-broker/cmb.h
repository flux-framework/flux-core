#define CMB_API_PATH	"/tmp/cmb_socket"
#define CMB_API_BUFSIZE	32768

typedef struct cmb_struct *cmb_t;

cmb_t cmb_init (void);
void cmb_fini (cmb_t c);

int cmb_send (cmb_t c, char *buf, int len);
int cmb_recv (cmb_t c, char *buf, int len, int *lenp);

int cmb_sendf (cmb_t c, const char *fmt, ...);
int cmb_recvs (cmb_t c, char **tagp, char **bodyp);

int cmb_subscribe (cmb_t c, char *s);
int cmb_unsubscribe (cmb_t c);

int cmb_ping (cmb_t c, int seq);
int cmb_snoop (cmb_t c, char *subscription);
int cmb_barrier (cmb_t c, char *name, int count, int nprocs, int procs_per_node);
int cmb_sync (cmb_t c);

int cmb_kvs_put (cmb_t c, char *key, char *val);
char *cmb_kvs_get (cmb_t c, char *key);
int cmb_kvs_commit (cmb_t c);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

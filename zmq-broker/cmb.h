#define CMB_API_PATH            "/tmp/cmb_socket"
#define CMB_API_BUFSIZE         32768
#define CMB_API_FD_BUFSIZE      (CMB_API_BUFSIZE - 1024)

typedef struct cmb_struct *cmb_t;

cmb_t cmb_init (void);
void cmb_fini (cmb_t c);

int cmb_ping (cmb_t c, int seq, int padding);
int cmb_snoop (cmb_t c, char *subscription);
int cmb_barrier (cmb_t c, char *name, int nprocs, int procs_per_node);
int cmb_sync (cmb_t c);

int cmb_kvs_put (cmb_t c, char *key, char *val);
char *cmb_kvs_get (cmb_t c, char *key);
int cmb_kvs_commit (cmb_t c, int *errcountp, int *putcountp);

int cmb_fd_open (cmb_t c, char *wname, char **np);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

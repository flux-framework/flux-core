#define CMB_API_PATH            "/tmp/cmb_socket"
#define CMB_API_BUFSIZE         32768
#define CMB_API_FD_BUFSIZE      (CMB_API_BUFSIZE - 1024)

typedef struct cmb_struct *cmb_t;

cmb_t cmb_init (void);
void cmb_fini (cmb_t c);

int cmb_ping (cmb_t c, char *tag, int seq, int padding, char **tagp);
int cmb_stats (cmb_t c, char *name, int *req, int *rep, int *event);

int cmb_event_subscribe (cmb_t c, char *subscription);
int cmb_event_unsubscribe (cmb_t c, char *subscription);
char *cmb_event_recv (cmb_t c);
int cmb_event_send (cmb_t c, char *event);

int cmb_barrier (cmb_t c, char *name, int nprocs);

int cmb_kvs_put (cmb_t c, const char *key, const char *val);
char *cmb_kvs_get (cmb_t c, const char *key);
int cmb_kvs_commit (cmb_t c, int *errcountp, int *putcountp);

int cmb_live_query (cmb_t c, int **up, int *ulp, int **dp, int *dlp, int *nnp);

int cmb_fd_open (cmb_t c, char *wname, char **np);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

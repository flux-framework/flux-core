#ifndef HAVE_CMBD_H
#define HAVE_CMBD_H
#include "cmb_socket.h"

#define MAX_PARENTS 2
typedef struct {
    char *upreq_uri;
    char *dnreq_uri;
    int rank;
} parent_t;

/* Config state.
 * This is static and can be shared by all threads.
 */
typedef struct {
    zhash_t *conf_hash;
    char *upreq_in_uri;
    char *dnreq_out_uri;
    char *upev_in_uri;
    char *upev_out_uri;
    char *dnev_in_uri;
    char *dnev_out_uri;
    bool verbose;
    char rankstr[16];
    bool treeroot;
    int rank;
    int size;
    char *plugins;
    parent_t parent[MAX_PARENTS];
    int parent_len;
    /* Options set in cmbd getopt and read by plugins.
     * FIXME: need a spank-like abstraction for plugin options.
     */
    char *api_sockpath;
} conf_t;

/* Server state.
 * This is dynamic and is only accessed by the main (zpoll) thread.
 */
typedef struct {
    zctx_t *zctx;
    void *zs_upreq_out;
    void *zs_dnreq_in;
    void *zs_upreq_in;
    void *zs_dnreq_out;
    void *zs_snoop;
    void *zs_upev_out;
    void *zs_upev_in;
    void *zs_dnev_out;
    void *zs_dnev_in;
    int parent_cur;
    bool parent_alive[MAX_PARENTS];
    zhash_t *plugins;
    route_ctx_t rctx;
    int epoch;
} server_t;

#define UPREQ_URI           "inproc://upreq"
#define DNREQ_URI           "inproc://dnreq"
#define UPREQ_MON_URI       "inproc://upreq_mon"

#define DNEV_OUT_URI        "inproc://evout"
#define DNEV_IN_URI         "inproc://evin"

#define SNOOP_URI           "inproc://snoop"

/* FIXME: paths should be configurable */
#define UPREQ_IPC_URI_TMPL  "ipc:///tmp/cmb_socket_upreq.uid%d"
#define DNREQ_IPC_URI_TMPL  "ipc:///tmp/cmb_socket_dnreq.uid%d"
#define EVOUT_IPC_URI_TMPL  "ipc:///tmp/cmb_socket_evout.uid%d"
#define EVIN_IPC_URI_TMPL   "ipc:///tmp/cmb_socket_evin.uid%d"
#define SNOOP_IPC_URI_TMPL  "ipc:///tmp/cmb_socket_snoop.uid%d"

void cmbd_log (conf_t *conf, server_t *srv, int lev, const char *fmt, ...);

#endif /* !HAVE_CMBD_H */

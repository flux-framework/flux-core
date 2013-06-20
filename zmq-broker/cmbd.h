typedef struct {
    char *treeout_uri; /* upreq_out connects to this port */
    char *treeout_uri2; /* dnreq_in connects to this port */
    int rank;
} parent_t;

enum {
    ROUTE_FLAGS_PRIVATE = 1,
};
typedef struct {
    char *gw;
    int flags;
} route_t;

typedef struct {
    char *treein_uri; /* upreq_in binds to this port */
    char *treein_uri2; /* dnreq_out binds to this port */
    parent_t parent[2];
    int parent_len;
    int *children;
    int children_len;
    char *event_in_uri;
    char *event_out_uri;
    bool verbose;
    int syncperiod_msec;
    char *redis_server;
    char *apisockpath;
    int rank;
    int size;
    char *plugins;
} conf_t;


/* 
Two dealer-router pairs exist to handle requests flowing upstream
and downstream.  An upstream request utilizes the default dealer-router
message flow: requests accrue address frames as they travel up.
Responses lose address frames (which direct routing) as they travel down.

Downstream requests utilize dealer-router in reverse.  The reason we do
not reverse socket roles is we need 'router' capability in the downward
flow (whether request or response) in order to direct messages to the
correct downstream peer.

A downstream request will naturally lose address frames at each hop,
but we want it to accrue them, so we push two frames, one containing the
local address, and one containing the address of the downstream peer.
The downstream address is stripped off and directs routing, the local
address remains part of the source route for the reply.  An upstream
response will naturally accrue frames, but we want them to lose frames,
so we strip two off at each hop and discard.

Requests traversing the tree may travel up then down.  Replies to these
requests also travel up and then down.  The decision to redirect an
upward-travelling request downward is based on looking up the destination
address embedded in the tag (N!tag) at each hop.  The decision to redirect
an upward-travelling reply downward is based on peeking at the top frame
(after discarding two).  If lookups in the route hash table are
successful, they are routed downstream as described above, otherwise
the request continues upstream, or is NAKed at the root.
 
  (dealer)              (dealer)
 +---------------------------------+    This is a simlified view of the
 | upreq_out             dnreq_in  |    cmbd sockets, focusing on those
 |                                 |    used in inter-node request/response
 |                                 |    message flow.
 | upreq_in              dnreq_out |
 +---------------------------------+
  (router)               (router)
*/

typedef struct {
    zctx_t *zctx;
    void *zs_eventout;
    void *zs_eventin;
    void *zs_upreq_out;
    void *zs_dnreq_in;
    void *zs_snoop;
    void *zs_plout_event;
    void *zs_plin;
    void *zs_upreq_in;
    void *zs_dnreq_out;
    void *zs_plin_event;
    void *zs_plin_tree;
    int parent_cur;
    zhash_t *route;
    zhash_t *plugins;
} server_t;

#define PLOUT_URI_TMPL      "inproc://plout_%s"
#define PLIN_URI            "inproc://plin"
#define ROUTER_URI          "inproc://router"
#define PLOUT_EVENT_URI     "inproc://plout_event"
#define PLIN_EVENT_URI      "inproc://plin_event"
#define SNOOP_URI           "inproc://snoop"

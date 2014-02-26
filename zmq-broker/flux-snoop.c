/* flux-snoop.c - flux snoop subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include <zmq.h>
#include <czmq.h>

#include "cmb.h"
#include "util.h"
#include "zmsg.h"
#include "log.h"

#if ZMQ_VERSION_MAJOR >= 4
#define HAVE_CURVE_SECURITY 1
#define HAVE_ZMONITOR 1
#endif

#if HAVE_CURVE_SECURITY
#include "curve.h"
#endif

#define DEFAULT_ZAP_DOMAIN  "flux"

#define OPTIONS "hanN:vl"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"all",        no_argument,        0, 'a'},
    {"no-security",no_argument,        0, 'n'},
    {"verbose",    no_argument,        0, 'v'},
    {"long",       no_argument,        0, 'l'},
    {"session-name",required_argument, 0, 'N'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-snoop OPTIONS [topic [topic...]]\n"
"  -a,--all                  Include suppressed cmb.info, log.msg messages\n"
#if HAVE_CURVE_SECURITY
"  -n,--no-security          Try to connect without CURVE security\n"
#endif
"  -v,--verbose              Verbose connect output\n"
"  -l,--long                 Display long message format\n"
"  -N,--session-name NAME    Set session name (default flux)\n"
);
    exit (1);
}

static void *connect_snoop (zctx_t *zctx, bool nopt, bool vopt,
                            const char *session, const char *uri);
static int snoop_cb (zloop_t *zloop, zmq_pollitem_t *item, void *arg);
#if HAVE_ZMONITOR
static int zmon_cb (zloop_t *zloop, zmq_pollitem_t *item, void *arg);
#endif

static bool aopt = false;
static bool lopt = false;

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *uri = NULL;
    bool nopt = false;
    bool vopt = false;
    char *session = "flux";
    zctx_t *zctx;
    zlist_t *subs;
    void *s;
#if HAVE_ZMONITOR
    zmonitor_t *zmon;
#endif
    zloop_t *zloop;
    zmq_pollitem_t zp;

    log_init ("flux-snoop");

    if (!(subs = zlist_new ()))
        oom ();
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'a': /* --all */
                aopt = true;
                break;
            case 'l': /* --long */
                lopt = true;
                break;
            case 'n': /* --no-security */
                nopt = true;;
                break;
            case 'v': /* --verbose */
                vopt = true;;
                break;
            case 'N': /* --session-name NAME */
                session = optarg;;
                break;
            default:
                usage ();
                break;
        }
    }
    while (optind < argc) {
        if (zlist_append (subs, argv[optind++]) < 0)
            oom ();
    }
    if (!(h = cmb_init ()))
        err_exit ("cmb_init");
    if (!(uri = flux_getattr (h, "cmbd-snoop-uri")))
        err_exit ("cmbd-snoop-uri");

    /* N.B. flux_get_zctx () is not implemented for the API socket since
     * it has no internal zctx (despite supporting the flux reactor).
     */
    if (!(zctx = zctx_new ()))
        err_exit ("zctx_new");
    zctx_set_linger (zctx, 5);

    /* N.B. We use the zloop reactor and handle disconnects via zmonitor.
     * If zmonitor is not available (zmq version < 4), restarting cmbd
     * will not be detected.  Also, as the snoop socket name may change
     * across restarts, we may hang forever reconnecting to the wrong socket.
     */
    if (!(zloop = zloop_new ()))
        oom ();

    /* Connect to the snoop socket and subscribe to topics of interest.
     */
    if (vopt)
        msg ("connecting to %s...", uri);
    s = connect_snoop (zctx, nopt, vopt, session, uri);
    zp.socket = s;
    zp.events = ZMQ_POLLIN;
    if (zloop_poller (zloop, &zp, snoop_cb, NULL) < 0)
        err_exit ("zloop_poller");
    if (zlist_size (subs) == 0)
        zsocket_set_subscribe (s, "");
    else {
        char *sub;
        while ((sub = zlist_pop (subs)))
            zsocket_set_subscribe (s, sub);
    }

#if HAVE_ZMONITOR
    if (!(zmon = zmonitor_new (zctx, s, ZMQ_EVENT_DISCONNECTED)))
        err_exit ("zmonitor_new");
    if (vopt)
        zmonitor_set_verbose (zmon, true);
    zp.socket = zmonitor_socket (zmon);
    zp.events = ZMQ_POLLIN;
    if (zloop_poller (zloop, &zp, zmon_cb, NULL) < 0)
        err_exit ("zloop_poller");
#endif

    if (zloop_start (zloop) < 0)
        err_exit ("zloop_start");
    if (vopt)
        msg ("disconnecting");

#if HAVE_ZMONITOR
    zmonitor_destroy (&zmon);
#endif

    zloop_destroy (&zloop);
    zctx_destroy (&zctx); /* destroys 's' */

    zlist_destroy (&subs);
    free (uri);
    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

static void *connect_snoop (zctx_t *zctx, bool nopt, bool vopt,
                            const char *session, const char *uri)
{
    void *s;

    if (!(s = zsocket_new (zctx, ZMQ_SUB)))
        err_exit ("zsocket_new");
#if HAVE_CURVE_SECURITY
    if (!nopt) {
        char *dir = flux_curve_getpath ();
        zauth_t *auth;
        zcert_t *cli_cert, *srv_cert;
        char *srvkey = NULL;

        if (!dir || flux_curve_checkpath (dir, false) < 0)
            exit (1);
        if (!(cli_cert = flux_curve_getcli (dir, session)))
            exit (1);
        if (!(srv_cert = flux_curve_getsrv (dir, session)))
            exit (1);

        if (!(auth = zauth_new (zctx)))
            err_exit ("zauth_new");
        if (vopt)
            zauth_set_verbose (auth, true);
        zauth_configure_curve (auth, "*", dir);

        zsocket_set_zap_domain (s, DEFAULT_ZAP_DOMAIN);
        zcert_apply (cli_cert, s);
        srvkey = zcert_public_txt (srv_cert);
        zsocket_set_curve_serverkey (s, srvkey);

        free (dir);
    }
#endif
    if (zsocket_connect (s, "%s", uri) < 0)
        err_exit ("%s", uri);
    return s;
}

/* The snoop socket includes two extra header frames:
 * First the tag frame, stripped of any node! prefix so subscriptions work.
 * Second, the message type as a stringified integer.
 */
static int snoop_cb (zloop_t *zloop, zmq_pollitem_t *item, void *arg)
{
    void *zs = item->socket;
    zmsg_t *zmsg;

    if ((zmsg = zmsg_recv (zs))) {
        char *tag = zmsg_popstr (zmsg);
        char *typestr = zmsg_popstr (zmsg);
        int type;

        if (tag && typestr) {
            if (aopt || (strcmp (tag, "cmb.info") && strcmp (tag, "log.msg"))) {
                if (lopt) {
                    zmsg_dump (zmsg);
                } else {
                    type = strtoul (typestr, NULL, 10);
                    zmsg_dump_compact (zmsg, flux_msgtype_shortstr (type));
                }
            }
        }
        if (tag)
            free (tag);
        if (typestr)
            free (typestr);
        zmsg_destroy (&zmsg);
    }
    return 0;
}

#if HAVE_ZMONITOR
static int zmon_cb (zloop_t *zloop, zmq_pollitem_t *item, void *arg)
{
    void *zs = item->socket;
    zmsg_t *zmsg;
    int event;
    char *s;

    if ((zmsg = zmsg_recv (zs))) {
        if ((s = zmsg_popstr (zmsg))) {
            event = strtoul (s, NULL, 10);
            if (event == ZMQ_EVENT_DISCONNECTED)
                msg_exit ("lost connection to snoop socket");
            free (s);
        }
        zmsg_destroy (&zmsg);
    }
    return 0;
}
#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

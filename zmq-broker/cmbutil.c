/* cmbutil.c - exercise public interfaces */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <stdbool.h>
#include <json/json.h>
#include <sys/param.h>

#include "cmb.h"
#include "log.h"
#include "util.h"
#include "zmsg.h"

static int _parse_logstr (char *s, int *lp, char **fp);
static void dump_kvs_dir (flux_t h, const char *path);

#define OPTIONS "p:s:b:B:k:SK:Ct:P:d:n:x:e:T:L:W:D:r:R:qz:Zyl:Y:X:M:"
static const struct option longopts[] = {
    {"ping",       required_argument,  0, 'p'},
    {"stats",      required_argument,  0, 'x'},
    {"ping-padding", required_argument,0, 'P'},
    {"ping-delay", required_argument,  0, 'd'},
    {"subscribe",  required_argument,  0, 's'},
    {"event",      required_argument,  0, 'e'},
    {"barrier",    required_argument,  0, 'b'},
    {"barrier-torture", required_argument,  0, 'B'},
    {"nprocs",     required_argument,  0, 'n'},
    {"kvs-put",    required_argument,  0, 'k'},
    {"kvs-get",    required_argument,  0, 'K'},
    {"kvs-list",   required_argument,  0, 'l'},
    {"kvs-watch",  required_argument,  0, 'Y'},
    {"kvs-watch-dir",required_argument,  0, 'X'},
    {"kvs-commit", no_argument,        0, 'C'},
    {"kvs-dropcache", no_argument,     0, 'y'},
    {"kvs-torture",required_argument,  0, 't'},
    {"mrpc-echo",  required_argument,  0, 'M'},
    {"sync",       no_argument,        0, 'S'},
    {"snoop",      required_argument,  0, 'T'},
    {"log",        required_argument,  0, 'L'},
    {"log-watch",  required_argument,  0, 'W'},
    {"log-dump",   required_argument,  0, 'D'},
    {"route-add",  required_argument,  0, 'r'},
    {"route-del",  required_argument,  0, 'R'},
    {"route-query",no_argument,        0, 'q'},
    {"socket-path",required_argument,  0, 'z'},
    {"trace-apisock",no_argument,      0, 'Z'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr, "Usage: cmbutil OPTIONS\n"
"  -p,--ping name         route a message through a plugin\n"
"  -P,--ping-padding N    pad ping packets with N bytes (adds a JSON string)\n"
"  -d,--ping-delay N      set delay between ping packets (in msec)\n"
"  -x,--stats name        get plugin statistics\n"
"  -T,--snoop topic       display messages to/from router socket\n"
"  -b,--barrier name      execute barrier across slurm job\n"
"  -B,--barrier-torture N execute N barriers across slurm job\n"
"  -n,--nprocs N          override nprocs (default $SLURM_NPROCS or 1)\n"
"  -k,--kvs-put key=val   set a key\n"
"  -K,--kvs-get key       get a key\n"
"  -Y,--kvs-watch key     watch a key (non-directory)\n"
"  -X,--kvs-watch-dir key watch a key (directory)\n"
"  -l,--kvs-list name     list keys in a particular \"directory\"\n"
"  -C,--kvs-commit        commit pending kvs puts\n"
"  -y,--kvs-dropcache     drop cached and unreferenced kvs data\n"
"  -t,--kvs-torture N     set N keys, then commit\n"
"  -M,--mrpc-echo NODES   exercise mrpc echo server (-P and -d apply)\n"
"  -s,--subscribe topic   subscribe to event topic\n"
"  -e,--event name        publish event\n"
"  -S,--sync              block until event.sched.triger\n"
"  -L,--log fac:lev MSG   log MSG to facility at specified level\n"
"  -W,--log-watch fac:lev watch logs for messages matching tag\n"
"  -D,--log-dump fac:lev  dump circular log buffer\n"
"  -r,--route-add dst:gw  add local route to dst via gw\n"
"  -R,--route-del dst     delete local route to dst\n"
"  -q,--route-query       list routes in JSON format\n"
"  -z,--socket-path PATH  use non-default API socket path\n"
"  -Z,--trace-apisock     trace api socket messages\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    flux_t h;
    int nprocs;
    int padding = 0;
    char *pad = NULL;
    int pingdelay_ms = 1000;
    static char socket_path[PATH_MAX + 1];
    char *val;
    bool Lopt = false;
    char *Lopt_facility;
    int Lopt_level;
    int flags = 0;

    log_init (basename (argv[0]));

    nprocs = env_getint ("SLURM_NPROCS", 1);

    if ((val = getenv ("CMB_API_PATH"))) {
        if (strlen (val) > PATH_MAX)
            err_exit ("What a long CMB_API_PATH you have!");
        strcpy (socket_path, val);
    }
    else {
        snprintf (socket_path, sizeof (socket_path),
                  CMB_API_PATH_TMPL, getuid ());
    }

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'P': /* --ping-padding N */
                if (pad)
                    free (pad);
                padding = strtoul (optarg, NULL, 10);
                if (padding > 0) {
                    pad = xzmalloc (padding + 1);
                    memset (pad, 'p', padding);
                }
                break;
            case 'd': /* --ping-delay N */
                pingdelay_ms = strtoul (optarg, NULL, 10);
                break;
            case 'n': /* --nprocs N */
                nprocs = strtoul (optarg, NULL, 10);
                break;
            case 'z': /* --socket-path PATH */
                snprintf (socket_path, sizeof (socket_path), "%s", optarg);
                break;
            case 'Z': /* --trace-apisock */
                flags |= FLUX_FLAGS_TRACE;
                break;
        }
    }
    if (!(h = cmb_init_full (socket_path)))
        err_exit ("cmb_init");
    flux_flags_set (h, flags);
    optind = 0;
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'P':
            case 'd':
            case 'n':
            case 'z':
            case 'Z':
                break; /* handled in first getopt */
            case 'p': { /* --ping name */
                int seq, rseq, rpadding;
                struct timespec t0;
                const char *route, *rpad;
                json_object *request = util_json_object_new_object ();
                json_object *response;

                if (pad)
                    util_json_object_add_string (request, "pad", pad);
                for (seq = 0; ; seq++) {
                    monotime (&t0);
                    json_object_object_del (request, "seq");
                    util_json_object_add_int (request, "seq", seq);
                    if (!(response = flux_rpc (h, request, "%s.ping", optarg)))
                        err_exit ("flux_rpc");
                    if (util_json_object_get_int (response, "seq", &rseq) < 0
                            || util_json_object_get_string (response,
                                                      "pad", &rpad) < 0
                            || util_json_object_get_string (response,
                                                      "route", &route) < 0)
                        msg_exit ("ping: pad, seq, or route missing");
                    if (seq != rseq)
                        msg_exit ("ping: seq not the one I sent");
                    if (padding != (rpadding = strlen (rpad)))
                        msg_exit ("ping: pad not the size I sent (%d != %d)",
                                                rpadding, padding);
                    msg ("%s.ping pad=%d seq=%d time=%0.3f ms (%s)", optarg,
                         rpadding, rseq, monotime_since (t0), route);
                    usleep (pingdelay_ms * 1000);
                    json_object_put (response);
                }
                json_object_put (request);
                break;
            }
            case 'x': { /* --stats name */
#if 0
                json_object *o;
                char *s;

                if (!(s = cmb_stats (h, optarg)))
                    err_exit ("cmb_stats");
                if (!(o = json_tokener_parse (s)))
                    err_exit ("json_tokener_parse");
                printf ("%s\n", json_object_to_json_string_ext (o,
                                    JSON_C_TO_STRING_PRETTY));
                json_object_put (o);
                free (s);
#endif
                break;
            }
            case 'b': { /* --barrier NAME */
                struct timespec t0;
                monotime (&t0);
                if (flux_barrier (h, optarg, nprocs) < 0)
                    err_exit ("flux_barrier");
                msg ("barrier time=%0.3f ms", monotime_since (t0));
                break;
            }
            case 'B': { /* --barrier-torture N */
                int i, n = strtoul (optarg, NULL, 10);
                char name[16];
                for (i = 0; i < n; i++) {
                    snprintf (name, sizeof (name), "%d", i);
                    if (flux_barrier (h, name, nprocs) < 0)
                        err_exit ("flux_barrier %s", name);
                }
                break;
            }
            case 's': { /* --subscribe topic */
                zmsg_t *zmsg;
                if (flux_event_subscribe (h, optarg) < 0)
                    err_exit ("flux_event_subscribe");
                while (flux_event_recvmsg (h, &zmsg, false) == 0) {
                    zmsg_dump_compact (zmsg);
                    zmsg_destroy (&zmsg);
                }
                err_exit ("flux_event_recvmsg");
                /*NOTREACHED*/
                if (flux_event_unsubscribe (h, optarg) < 0)
                    err_exit ("flux_event_unsubscribe");
                break;
            }
            case 'T': { /* --snoop topic */
                zmsg_t *zmsg;
                if (flux_snoop_subscribe (h, optarg) < 0)
                    err_exit ("flux_snoop_subscribe");
                while (flux_snoop_recvmsg (h, &zmsg, false) == 0) {
                    zmsg_dump_compact (zmsg);
                    zmsg_destroy (&zmsg);
                }
                err_exit ("flux_snoop_recvmsg");
                /*NOTREACHED*/
                if (flux_snoop_unsubscribe (h, optarg) < 0)
                    err_exit ("flux_snoop_unsubscribe");
                break;
            }
            case 'S': { /* --sync */
                zmsg_t *zmsg;
                if (flux_event_subscribe (h, "event.sched.trigger.") < 0)
                    err_exit ("flux_event_subscribe");
                if (flux_event_recvmsg (h, &zmsg, false) < 0)
                    err_exit ("flux_event_recvmsg");
                zmsg_destroy (&zmsg);
                break;
            }
            case 'e': { /* --event name */
                if (flux_event_send (h, NULL, "%s", optarg) < 0)
                    err_exit ("flux_event_send");
                break;
            }
            case 'k': { /* --kvs-put key=val */
                char *key = optarg;
                char *val = strchr (optarg, '=');
                json_object *vo = NULL;

                if (!val)
                    msg_exit ("malformed key=[val] argument");
                *val++ = '\0';
                if (strlen (val) > 0)
                    if (!(vo = json_tokener_parse (val)))
                        vo = json_object_new_string (val);
                if (kvs_put (h, key, vo) < 0)
                    err_exit ("kvs_put");
                if (vo)
                    json_object_put (vo);
                break;

            }
            case 'K': { /* --kvs-get key */
                json_object *o;

                if (kvs_get (h, optarg, &o) < 0)
                    err_exit ("kvs_get");
                printf ("%s = %s\n", optarg, json_object_to_json_string_ext (o,
                                             JSON_C_TO_STRING_PLAIN));
                json_object_put (o);
                break;
            }
            case 'Y': { /* --kvs-watch key */
                json_object *val = NULL;
                int rc;

                rc = kvs_get (h, optarg, &val);
                while (rc == 0 || (rc < 0 && errno == ENOENT)) {
                    if (rc < 0) {
                        printf ("%s: %s\n", optarg, strerror (errno));
                        if (val)
                            json_object_put (val);
                        val = NULL;
                    } else
                        printf ("%s=%s\n", optarg,
                                json_object_to_json_string_ext (val,
                                JSON_C_TO_STRING_PLAIN));
                    rc = kvs_watch_once (h, optarg, &val);
                }
                err_exit ("%s", optarg);
                break;
            }
            case 'X': { /* --kvs-watch-dir key */
                kvsdir_t dir = NULL;
                int rc;

                rc = kvs_get_dir (h, &dir, "%s", optarg);
                while (rc == 0 || (rc < 0 && errno == ENOENT)) {
                    if (rc < 0) {
                        printf ("%s: %s\n", optarg, strerror (errno));
                        if (dir)
                            kvsdir_destroy (dir);
                        dir = NULL;
                    } else {
                        dump_kvs_dir (h, optarg);
                        printf ("======================\n");
                    }
                    rc = kvs_watch_once_dir (h, &dir, "%s", optarg);
                } 
                err_exit ("%s", optarg);
                break;
            }
            case 'l': { /* --kvs-list name */
                dump_kvs_dir (h, optarg);
                break;
            }
            case 'C': { /* --kvs-commit */
                if (kvs_commit (h) < 0)
                    err_exit ("kvs_commit");
                break;
            }
            case 'y': { /* --kvs-dropcache */
                if (kvs_dropcache (h) < 0)
                    err_exit ("kvs_dropcache");
                break;
            }
            case 't': { /* --kvs-torture N */
                int i, n = strtoul (optarg, NULL, 10);
                char key[16], val[16];
                struct timespec t0;
                json_object *vo = NULL;

                monotime (&t0);
                for (i = 0; i < n; i++) {
                    snprintf (key, sizeof (key), "key%d", i);
                    snprintf (val, sizeof (key), "val%d", i);
                    vo = json_object_new_string (val);
                    if (kvs_put (h, key, vo) < 0)
                        err_exit ("kvs_put");
                    if (vo)
                        json_object_put (vo);
                }
                msg ("kvs_put:    time=%0.3f ms", monotime_since (t0));

                monotime (&t0);
                if (kvs_commit (h) < 0)
                    err_exit ("kvs_commit");
                msg ("kvs_commit: time=%0.3f ms", monotime_since (t0));

                monotime (&t0);
                for (i = 0; i < n; i++) {
                    snprintf (key, sizeof (key), "key%d", i);
                    snprintf (val, sizeof (key), "val%d", i);
                    if (kvs_get (h, key, &vo) < 0)
                        err_exit ("kvs_get");
                    if (strcmp (json_object_get_string (vo), val) != 0)
                        msg_exit ("kvs_get: key '%s' wrong value '%s'",
                                  key, json_object_get_string (vo));
                    if (vo)
                        json_object_put (vo);
                }
                msg ("kvs_get:    time=%0.3f ms", monotime_since (t0));
                break;
            }
            case 'L': { /* --log */
                if (_parse_logstr (optarg, &Lopt_level, &Lopt_facility) < 0)
                    msg_exit ("bad log level string");
                Lopt = true; /* see code after getopt */
                break;
            }
            case 'W': { /* --log-watch fac:lev */
#if 0
                char *src, *fac, *s;
                struct timeval tv, start = { .tv_sec = 0 }, rel;
                int count, lev;
                const char *levstr;

                if (_parse_logstr (optarg, &lev, &fac) < 0)
                    msg_exit ("bad log level string");
                if (cmb_log_subscribe (c, lev, fac) < 0)
                    err_exit ("cmb_log_subscribe");
                free (fac);
                while ((s = cmb_log_recv (c, &lev, &fac, &count, &tv, &src))) {
                    if (start.tv_sec == 0)
                        start = tv;
                    timersub (&tv, &start, &rel);
                    levstr = log_leveltostr (lev);
                    //printf ("XXX lev=%d (%s)\n", lev, levstr);
                    fprintf (stderr, "[%-.6lu.%-.6lu] %dx %s.%s[%s]: %s\n",
                             rel.tv_sec, rel.tv_usec, count,
                             fac, levstr ? levstr : "unknown", src, s);
                    free (fac);
                    free (src);
                    free (s);
                }
                if (errno != ENOENT)
                    err ("cmbv_log_recv");
#endif
                break;
            }
            case 'D': { /* --log-dump fac:lev */
#if 0
                char *src, *fac, *s;
                struct timeval tv, start = { .tv_sec = 0 }, rel;
                int lev, count;
                const char *levstr;

                if (_parse_logstr (optarg, &lev, &fac) < 0)
                    msg_exit ("bad log level string");
                if (cmb_log_dump (c, lev, fac) < 0)
                    err_exit ("cmb_log_dump");
                free (fac);
                while ((s = cmb_log_recv (c, &lev, &fac, &count, &tv, &src))) {
                    if (start.tv_sec == 0)
                        start = tv;
                    timersub (&tv, &start, &rel);
                    levstr = log_leveltostr (lev);
                    fprintf (stderr, "[%-.6lu.%-.6lu] %dx %s.%s[%s]: %s\n",
                             rel.tv_sec, rel.tv_usec, count,
                             fac, levstr ? levstr : "unknown", src, s);
                    free (fac);
                    free (src);
                    free (s);
                }
                if (errno != ENOENT)
                    err ("cmbv_log_recv");
#endif
                break;
            }
            case 'r': { /* --route-add dst:gw */
#if 0
                char *gw, *dst = xstrdup (optarg);
                if (!(gw = strchr (dst, ':')))
                    usage ();
                *gw++ = '\0';
                if (cmb_route_add (c, dst, gw) < 0)
                    err ("cmb_route_add %s via %s", dst, gw);
#endif
                break;
            }
            case 'R': { /* --route-del dst */
#if 0
                char *gw, *dst = xstrdup (optarg);
                if (!(gw = strchr (dst, ':')))
                    usage ();
                *gw++ = '\0';
                if (cmb_route_del (c, dst, gw) < 0)
                    err ("cmb_route_del %s via %s", dst, gw);
#endif
                break;
            }
            case 'q': { /* --route-query */
#if 0
                json_object *o;
                char *s;

                if (!(s = cmb_route_query (c)))
                    err_exit ("cmb_route_query");
                if (!(o = json_tokener_parse (s)))
                    err_exit ("json_tokener_parse");
                printf ("%s\n", json_object_to_json_string_ext (o,
                                    JSON_C_TO_STRING_PRETTY));
                json_object_put (o);
                free (s);
                msg ("rank=%d size=%d", flux_rank (c), flux_size (c));
#endif
                break;
            }
            case 'M': { /* --mrpc-echo NODELIST */
                flux_mrpc_t f;
                json_object *inarg, *outarg;
                int id;
                struct timespec t0;
                int seq;

                for (seq = 0; ;seq++) {
                    monotime (&t0); 
                    if (!(f = flux_mrpc_create (h, optarg)))
                        err_exit ("flux_mrpc_create");
                    inarg = util_json_object_new_object ();
                    util_json_object_add_int (inarg, "seq", seq);
                    if (pad)
                        util_json_object_add_string (inarg, "pad", pad);
                    flux_mrpc_put_inarg (f, inarg);
                    if (flux_mrpc (f, "mecho") < 0)
                        err_exit ("flux_mrpc");
                    while ((id = flux_mrpc_next_outarg (f)) != -1) {
                        if (flux_mrpc_get_outarg (f, id, &outarg) < 0) {
                            msg ("%d: no response", id);
                            continue;
                        }
                        if (!util_json_match (inarg, outarg))
                            msg ("%d: mangled response", id);
                        json_object_put (outarg);
                    }
                    json_object_put (inarg);
                    flux_mrpc_destroy (f);
                    msg ("mecho: pad=%d seq=%d time=%0.3f ms",
                         padding, seq, monotime_since (t0));
                    usleep (pingdelay_ms * 1000);
                }
                if (pad)
                    free (pad);
                break;
            }
            default:
                usage ();
        }
    }
    if (Lopt) {
        char *argstr = argv_concat (argc - optind, argv + optind);
#if 0
        cmb_log_set_facility (c, Lopt_facility);
        if (cmb_log (c, Lopt_level, "%s", argstr) < 0)
            err_exit ("cmb_log");
#endif
        free (argstr);
    } else {
        if (optind < argc)
            usage ();
    }

    flux_handle_destroy (&h);
    exit (0);
}

static int _parse_logstr (char *s, int *lp, char **fp)
{
    char *p, *fac = xstrdup (s);
    int lev = LOG_INFO;

    if ((p = strchr (fac, ':'))) {
        *p++ = '\0';
        lev = log_strtolevel (p);
        if (lev < 0)
            return -1;
    }
    *lp = lev;
    *fp = fac;
    return 0;
}

static void dump_kvs_dir (flux_t h, const char *path)
{
    kvsdir_t dir;
    kvsitr_t itr;
    const char *name, *js;
    char *key;

    if (kvs_get_dir (h, &dir, "%s", path) < 0) {
        printf ("%s: %s\n", path, strerror (errno));
        return;
    }

    itr = kvsitr_create (dir);
    while ((name = kvsitr_next (itr))) {
        key = kvsdir_key_at (dir, name);
        if (kvsdir_issymlink (dir, name)) {
            char *link;

            if (kvs_get_symlink (h, key, &link) < 0) {
                printf ("%s: %s\n", key, strerror (errno));
                continue;
            }
            printf ("%s -> %s\n", key, link);
            free (link);

        } else if (kvsdir_isdir (dir, name)) {
            dump_kvs_dir (h, key);

        } else {
            json_object *o;
            int len, max;

            if (kvs_get (h, key, &o) < 0) {
                printf ("%s: %s\n", key, strerror (errno));
                continue;
            }
            js = json_object_to_json_string_ext (o, JSON_C_TO_STRING_PLAIN);
            len = strlen (js);
            max = 80 - strlen (key) - 4;
            if (len > max)
                printf ("%s = %.*s ...\n", key, max - 4, js);
            else
                printf ("%s = %s\n", key, js);
            json_object_put (o);
        }
        free (key);
    }
    kvsitr_destroy (itr);
    kvsdir_destroy (dir);
}
#if 0
static char *cmb_stats (flux_t h, char *name)
{
    json_object *o = util_json_object_new_object ();
    char *cpy = NULL;

    /* send request */
    if (_send_message (c, o, "%s.stats", name) < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    if (!o)
        goto eproto;
    cpy = strdup (json_object_get_string (o));
    if (!cpy) {
        errno = ENOMEM;
        goto error;
    }
    json_object_put (o);
    return cpy;
eproto:
    errno = EPROTO;
error:    
    if (cpy)
        free (cpy);
    if (o)
        json_object_put (o);
    return NULL;
}
#endif

#if 0
int cmb_route_add (cmb_t c, char *dst, char *gw)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_string (o, "gw", gw);
    if (_send_message (c, o, "cmb.route.add.%s", dst) < 0)
        goto error;
    json_object_put (o);
    return 0;
error:
    json_object_put (o);
    return -1;
}

int cmb_route_del (cmb_t c, char *dst, char *gw)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_string (o, "gw", gw);
    if (_send_message (c, o, "cmb.route.del.%s", dst) < 0)
        goto error;
    json_object_put (o);
    return 0;
error:
    json_object_put (o);
    return -1;
}

/* FIXME: just return JSON string for now */
char *cmb_route_query (cmb_t c)
{
    json_object *o = util_json_object_new_object ();
    char *cpy;

    /* send request */
    if (_send_message (c, o, "cmb.route.query") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    cpy = xstrdup (json_object_get_string (o));
    json_object_put (o);
    return cpy;
error:
    if (o)
        json_object_put (o);
    return NULL;

}
#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

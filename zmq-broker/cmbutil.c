/* cmbutil.c - exercise public interfaces */

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

#include "cmb.h"
#include "log.h"
#include "util.h"

static void _parse_logstr (char *s, logpri_t *pp, char **fp);

#define OPTIONS "p:s:b:B:k:SK:Ct:P:d:n:lx:e:TL:W:D:r:R:qz:Za:A:"
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
    {"kvs-commit", no_argument,        0, 'C'},
    {"kvs-torture",required_argument,  0, 't'},
    {"sync",       no_argument,        0, 'S'},
    {"live-query", no_argument,        0, 'l'},
    {"snoop",      no_argument,        0, 'T'},
    {"log",        required_argument,  0, 'L'},
    {"log-watch",  required_argument,  0, 'W'},
    {"log-dump",   required_argument,  0, 'D'},
    {"route-add",  required_argument,  0, 'r'},
    {"route-del",  required_argument,  0, 'R'},
    {"route-query",no_argument,        0, 'q'},
    {"socket-path",required_argument,  0, 'z'},
    {"trace-apisock",no_argument,      0, 'Z'},
    {"conf-get",   required_argument,  0, 'a'},
    {"conf-put",   required_argument,  0, 'A'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr, "Usage: cmbutil OPTIONS\n"
"  -p,--ping name         route a message through a plugin\n"
"  -P,--ping-padding N    pad ping packets with N bytes (adds a JSON string)\n"
"  -d,--ping-delay N      set delay between ping packets (in msec)\n"
"  -x,--stats name        get plugin statistics\n"
"  -T,--snoop             display messages to/from router socket\n"
"  -b,--barrier name      execute barrier across slurm job\n"
"  -B,--barrier-torture N execute N barriers across slurm job\n"
"  -n,--nprocs N          override nprocs (default $SLURM_NPROCS or 1)\n"
"  -k,--kvs-put key=val   set a key\n"
"  -K,--kvs-get key       get a key\n"
"  -C,--kvs-commit        commit pending kvs puts\n"
"  -t,--kvs-torture N     set N keys, then commit\n"
"  -A,--conf-put key=val  set a config key\n"
"  -a,--conf-get key      get a conf key\n"
"  -s,--subscribe sub     subscribe to events matching substring\n"
"  -e,--event name        publish event\n"
"  -S,--sync              block until event.sched.triger\n"
"  -l,--live-query        get list of up nodes\n"
"  -L,--log fac:pri MSG   log MSG to facility at specified priority\n"
"  -W,--log-watch fac:pri watch logs for messages matching tag\n"
"  -D,--log-dump fac:pri  dump circular log buffer\n"
"  -r,--route-add dst:gw  add local route to dst via gw\n"
"  -R,--route-del dst     delete local route to dst\n"
"  -q,--route-query       list routes in JSON format\n"
"  -z,--socket-path PATH  use non-default API socket path\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    cmb_t c;
    int nprocs;
    int padding = 0;
    int pingdelay_ms = 1000;
    char *socket_path = CMB_API_PATH;
    bool Lopt = false;
    char *Lopt_facility;
    logpri_t Lopt_priority;
    int flags = 0;

    log_init (basename (argv[0]));

    nprocs = env_getint ("SLURM_NPROCS", 1);

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'P': /* --ping-padding N */
                padding = strtoul (optarg, NULL, 10);
                break;
            case 'd': /* --ping-delay N */
                pingdelay_ms = strtoul (optarg, NULL, 10);
                break;
            case 'n': /* --nprocs N */
                nprocs = strtoul (optarg, NULL, 10);
                break;
            case 'z': /* --socket-path PATH */
                socket_path = optarg;
                break;
            case 'Z': /* --trace-apisock */
                flags |= CMB_FLAGS_TRACE;
                break;
        }
    }
    if (!(c = cmb_init_full (socket_path, flags)))
        err_exit ("cmb_init");
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
                int i;
                struct timeval t, t1, t2;
                char *tag, *route;
                for (i = 0; ; i++) {
                    xgettimeofday (&t1, NULL);
                    if (cmb_ping (c, optarg, i, padding, &tag, &route) < 0)
                        err_exit ("cmb_ping");
                    xgettimeofday (&t2, NULL);
                    timersub (&t2, &t1, &t);
                    msg ("%s pad=%d seq=%d time=%0.3f ms (%s)", tag,
                         padding,i,
                         (double)t.tv_sec * 1000 + (double)t.tv_usec / 1000,
                         route);
                    usleep (pingdelay_ms * 1000);
                    free (tag);
                    free (route);
                }
                break;
            }
            case 'x': { /* --stats name */
                json_object *o;
                char *s;

                if (!(s = cmb_stats (c, optarg)))
                    err_exit ("cmb_stats");
                if (!(o = json_tokener_parse (s)))
                    err_exit ("json_tokener_parse");
                printf ("%s\n", json_object_to_json_string_ext (o,
                                    JSON_C_TO_STRING_PRETTY));
                json_object_put (o);
                free (s);
                break;
            }
            case 'b': { /* --barrier NAME */
                struct timeval t, t1, t2;
                xgettimeofday (&t1, NULL);
                if (cmb_barrier (c, optarg, nprocs) < 0)
                    err_exit ("cmb_barrier");
                xgettimeofday (&t2, NULL);
                timersub (&t2, &t1, &t);
                msg ("barrier time=%0.3f ms",
                     (double)t.tv_sec * 1000 + (double)t.tv_usec / 1000);
                break;
            }
            case 'B': { /* --barrier-torture N */
                int i, n = strtoul (optarg, NULL, 10);
                char name[16];
                for (i = 0; i < n; i++) {
                    snprintf (name, sizeof (name), "%d", i);
                    if (cmb_barrier (c, name, nprocs) < 0)
                        err_exit ("cmb_barrier %s", name);
                }
                break;
            }
            case 's': { /* --subscribe substr */
                char *event;
                if (cmb_event_subscribe (c, optarg) < 0)
                    err_exit ("cmb_event_subscribe");
                while ((event = cmb_event_recv (c))) {
                    msg ("%s", event);
                    free (event);
                }
                break;
            }
            case 'T': { /* --snoop */
                if (cmb_snoop (c, true) < 0)
                    err_exit ("cmb_snoop");
                while (cmb_snoop_one (c) == 0)
                    ;
                /* NOTREACHED */
                break;
            }
            case 'S': { /* --sync */
                char *event;
                if (cmb_event_subscribe (c, "event.sched.trigger.") < 0)
                    err_exit ("cmb_event_subscribe");
                if (!(event = cmb_event_recv (c)))
                    err_exit ("cmb_event_recv");
                free (event);
                break;
            }
            case 'e': { /* --event name */
                if (cmb_event_send (c, optarg) < 0)
                    err_exit ("cmb_event_send");
                break;
            }

            case 'l': { /* --live-query */
                int i, *up = NULL, up_len, *dn = NULL, dn_len, nnodes;
                if (cmb_live_query (c, &up, &up_len, &dn, &dn_len, &nnodes) < 0)
                    err_exit ("cmb_live_query");
                printf ("up:   ");
                for (i = 0; i < up_len; i++)
                    printf ("%d%s", up[i], i == up_len - 1 ? "" : ",");
                printf ("\ndown: ");
                for (i = 0; i < dn_len; i++)
                    printf ("%d%s", dn[i], i == dn_len - 1 ? "" : ",");
                printf ("\n");
                if (up)
                    free (up); 
                if (dn)
                    free (dn); 
                break;
            }
            case 'k': { /* --kvs-put key=val */
                char *key = optarg;
                char *val = strchr (optarg, '=');
                if (val == NULL)
                    usage ();
                *val++ = '\0';
                if (cmb_kvs_put (c, key, val) < 0)
                    err_exit ("cmb_kvs_put");
                break;
            }
            case 'K': { /* --kvs-get key */
                char *val = cmb_kvs_get (c, optarg);
                if (!val && errno != 0)
                    err_exit ("cmb_kvs_get");
                printf ("%s=%s\n", optarg, val ? val : "<nil>");
                if (val)
                    free (val);
                break;
            }
            case 'C': { /* --kvs-commit */
                int errcount, putcount;

                if (cmb_kvs_commit (c, &errcount, &putcount) < 0)
                    err_exit ("cmb_kvs_commit");
                printf ("errcount=%d putcount=%d\n", errcount, putcount);
                break;
            }
            case 't': { /* --kvs-torture N */
                int i, n = strtoul (optarg, NULL, 10);
                char key[16], val[16], *rval;
                struct timeval t1, t2, t;
                int errcount, putcount;

                xgettimeofday (&t1, NULL);
                for (i = 0; i < n; i++) {
                    snprintf (key, sizeof (key), "key%d", i);
                    snprintf (val, sizeof (key), "val%d", i);
                    if (cmb_kvs_put (c, key, val) < 0)
                        err_exit ("cmb_kvs_put");
                }
                xgettimeofday (&t2, NULL);
                timersub(&t2, &t1, &t);
                msg ("kvs_put:    time=%0.3f ms",
                     (double)t.tv_sec * 1000 + (double)t.tv_usec / 1000);

                xgettimeofday (&t1, NULL);
                if (cmb_kvs_commit (c, &errcount, &putcount) < 0)
                    err_exit ("cmb_kvs_commit");
                xgettimeofday (&t2, NULL);
                timersub (&t2, &t1, &t);
                msg ("kvs_commit: time=%0.3f ms errcount=%d putcount=%d",
                     (double)t.tv_sec * 1000 + (double)t.tv_usec / 1000,
                     errcount, putcount);

                xgettimeofday (&t1, NULL);
                for (i = 0; i < n; i++) {
                    snprintf (key, sizeof (key), "key%d", i);
                    snprintf (val, sizeof (key), "val%d", i);
                    if (!(rval = cmb_kvs_get (c, key)))
                        err_exit ("cmb_kvs_get");
                    if (strcmp (rval, val) != 0)
                        msg_exit ("cmb_kvs_get: incorrect val");
                    free (rval);
                }
                xgettimeofday (&t2, NULL);
                timersub(&t2, &t1, &t);
                msg ("kvs_get:    time=%0.3f ms",
                     (double)t.tv_sec * 1000 + (double)t.tv_usec / 1000);

                break;
            }
            case 'A': { /* --conf-put key=val */
                char *key = optarg;
                char *val = strchr (optarg, '=');
                if (val == NULL)
                    usage ();
                *val++ = '\0';
                if (cmb_conf_put (c, key, val) < 0)
                    err_exit ("cmb_conf_put");
                break;
            }
            case 'a': { /* --conf-get key */
                char *val = cmb_conf_get (c, optarg);
                if (!val && errno != 0)
                    err_exit ("cmb_conf_get");
                printf ("%s=%s\n", optarg, val ? val : "<nil>");
                if (val)
                    free (val);
                break;
            }
            case 'L': { /* --log */
                Lopt = true;
                _parse_logstr (optarg, &Lopt_priority, &Lopt_facility);
                break;
            }
            case 'W': {
                char *src, *fac, *s;
                struct timeval tv, start = { .tv_sec = 0 }, rel;
                logpri_t pri;
                int count;

                _parse_logstr (optarg, &pri, &fac);
                if (cmb_log_subscribe (c, pri, fac) < 0)
                    err_exit ("cmb_log_subscribe");
                free (fac);
                while ((s = cmb_log_recv (c, &pri, &fac, &count, &tv, &src))) {
                    if (start.tv_sec == 0)
                        start = tv;
                    timersub (&tv, &start, &rel);
                    fprintf (stderr, "[%-.6lu.%-.6lu] %dx %s.%s[%s]: %s\n",
                             rel.tv_sec, rel.tv_usec, count,
                             fac, util_logpri_str(pri), src, s);
                    free (fac);
                    free (src);
                    free (s);
                }
                if (errno != ENOENT)
                    err ("cmbv_log_recv");
                break;
            }
            case 'D': {
                char *src, *fac, *s;
                struct timeval tv, start = { .tv_sec = 0 }, rel;
                logpri_t pri;
                int count;

                _parse_logstr (optarg, &pri, &fac);
                if (cmb_log_dump (c, pri, fac) < 0)
                    err_exit ("cmb_log_dump");
                free (fac);
                while ((s = cmb_log_recv (c, &pri, &fac, &count, &tv, &src))) {
                    if (start.tv_sec == 0)
                        start = tv;
                    timersub (&tv, &start, &rel);
                    fprintf (stderr, "[%-.6lu.%-.6lu] %dx %s.%s[%s]: %s\n",
                             rel.tv_sec, rel.tv_usec, count,
                             fac, util_logpri_str(pri), src, s);
                    free (fac);
                    free (src);
                    free (s);
                }
                if (errno != ENOENT)
                    err ("cmbv_log_recv");
                break;
            }
            case 'r': { /* --route-add dst:gw */
                char *gw, *dst = xstrdup (optarg);
                if (!(gw = strchr (dst, ':')))
                    usage ();
                *gw++ = '\0';
                if (cmb_route_add (c, dst, gw) < 0)
                    err ("cmb_route_add %s via %s", dst, gw);
                break;
            }
            case 'R': { /* --route-del dst */
                char *gw, *dst = xstrdup (optarg);
                if (!(gw = strchr (dst, ':')))
                    usage ();
                *gw++ = '\0';
                if (cmb_route_del (c, dst, gw) < 0)
                    err ("cmb_route_del %s via %s", dst, gw);
                break;
            }
            case 'q': { /* --route-query */
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
                break;
            }
            default:
                usage ();
        }
    }

    if (Lopt) {
        char *argstr = argv_concat (argc - optind, argv + optind);

        if (cmb_log (c, Lopt_priority, Lopt_facility, "%s", argstr) < 0)
            err_exit ("cmb_log");
        free (argstr);
    } else {
        if (optind < argc)
            usage ();
    }

    cmb_fini (c);
    exit (0);
}

static void _parse_logstr (char *s, logpri_t *pp, char **fp)
{
    char *p, *fac = xstrdup (s);
    logpri_t pri = CMB_LOG_INFO;

    if ((p = strchr (fac, ':'))) {
        *p++ = '\0';
        pri = util_logpri_val (p);
    }
    *pp = pri;
    *fp = fac;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

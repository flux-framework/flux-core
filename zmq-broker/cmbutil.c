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

#define OPTIONS "p:s:b:k:SK:Ct:P:d:n:lx:e:TL:W:r:R:qz:"
static const struct option longopts[] = {
    {"ping",       required_argument,  0, 'p'},
    {"stats",      required_argument,  0, 'x'},
    {"ping-padding", required_argument,0, 'P'},
    {"ping-delay", required_argument,  0, 'd'},
    {"subscribe",  required_argument,  0, 's'},
    {"event",      required_argument,  0, 'e'},
    {"barrier",    required_argument,  0, 'b'},
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
    {"route-add",  required_argument,  0, 'r'},
    {"route-del",  required_argument,  0, 'R'},
    {"route-query",no_argument,        0, 'q'},
    {"socket-path",required_argument,  0, 'z'},
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
"  -n,--nprocs N          override nprocs (default $SLURM_NPROCS or 1)\n"
"  -k,--kvs-put key=val   set a key\n"
"  -K,--kvs-get key       get a key\n"
"  -C,--kvs-commit        commit pending kvs puts\n"
"  -t,--kvs-torture N     set N keys, then commit\n"
"  -s,--subscribe sub     subscribe to events matching substring\n"
"  -e,--event name        publish event\n"
"  -S,--sync              block until event.sched.triger\n"
"  -l,--live-query        get list of up nodes\n"
"  -L,--log MSG           log MSG\n"
"  -W,--log-watch tag     watch logs for messages matching tag\n"
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
        }
    }
    if (!(c = cmb_init_full (socket_path, 0)))
        err_exit ("cmb_init");
    optind = 0;
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
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
            case 'P':
            case 'd':
            case 'n':
            case 'z':
                break; /* handled in first getopt */
            case 'L': { /* --log */
                if (cmb_log (c, "cmbutil", NULL, "%s", optarg) < 0)
                    err_exit ("cmb_log");
                break;
            }
            case 'W': {
                char *s, *t, *ss;
                struct timeval tv, start, rel;

                xgettimeofday (&start, NULL);
                if (cmb_log_subscribe (c, optarg) < 0)
                    err_exit ("cmb_log_subscribe");
                while ((s = cmb_log_recv (c, &t, &tv, &ss))) {
                    timersub (&tv, &start, &rel);
                    fprintf (stderr, "[%-.6lu.%-.6lu] %s[%s]: %s\n",
                             rel.tv_sec, rel.tv_usec, t, ss, s);
                    free (s);
                    free (t);
                }
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
    if (optind < argc)
        usage ();

    cmb_fini (c);
    exit (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

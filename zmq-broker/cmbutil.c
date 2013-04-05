/* cmbutil.c - exercise public interfaces */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#include "cmb.h"

#define OPTIONS "psb:f:k:SK:Ct:P:d:"
static const struct option longopts[] = {
    {"ping",       no_argument,        0, 'p'},
    {"ping-padding", required_argument,0, 'P'},
    {"ping-delay", required_argument,  0, 'd'},
    {"snoop",      no_argument,        0, 's'},
    {"barrier",    required_argument,  0, 'b'},
    {"flood",      required_argument,  0, 'f'},
    {"kvs-put",    required_argument,  0, 'k'},
    {"kvs-get",    required_argument,  0, 'K'},
    {"kvs-commit", no_argument,        0, 'C'},
    {"kvs-torture",required_argument,  0, 't'},
    {"sync",       no_argument,        0, 'S'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr, "Usage: cmbutil OPTIONS\n"
"  -p,--ping            loop back a sequenced message through the cmb\n"
"  -P,--ping-padding N  pad ping packets with N bytes (adds a JSON string)\n"
"  -d,--ping-delay N    set delay between ping packets (in msec)\n"
"  -s,--snoop SUB       watch traffic on the cmb (SUB=\"\" for all)\n"
"  -b,--barrier name    execute barrier across slurm job\n"
"  -k,--kvs-put key=val set a key\n"
"  -K,--kvs-get key     get a key\n"
"  -C,--kvs-commit      commit pending kvs puts\n"
"  -t,--kvs-torture N   set N keys, then commit\n"
"  -S,--sync            block until event.sched.triger\n");
    exit (1);
}

static int _env_getint (char *name, int dflt)
{
    char *ev = getenv (name);
    return ev ? strtoul (ev, NULL, 10) : dflt;
}


int main (int argc, char *argv[])
{
    int ch;
    cmb_t c;
    int nprocs;
    int tasks_per_node;
    int padding = 0;
    int pingdelay_ms = 1000;

    nprocs = _env_getint ("SLURM_NPROCS", 1);
    tasks_per_node = _env_getint ("SLURM_TASKS_PER_NODE", 1);

    if (!(c = cmb_init ())) {
        fprintf (stderr, "cmb_init: %s\n", strerror (errno));
        exit (1);
    }
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'P': /* --ping-padding N */
                padding = strtoul (optarg, NULL, 10);
                break;
            case 'd': /* --ping-delay N */
                pingdelay_ms = strtoul (optarg, NULL, 10);
                break;
        }
    }
    optind = 0;
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'p': { /* --ping */
                int i;
                struct timeval t, t1, t2;
                for (i = 0; ; i++) {
                    gettimeofday (&t1, NULL);
                    if (cmb_ping (c, i, padding) < 0) {
                        fprintf (stderr, "cmb_ping: %s\n", strerror(errno));
                        exit (1);
                    }
                    gettimeofday (&t2, NULL);
                    timersub (&t2, &t1, &t);
                    fprintf (stderr,
                      "loopback ping pad=%d seq=%d time=%0.3f ms\n", padding, i,
                      (double)t.tv_sec * 1000 + (double)t.tv_usec / 1000);
                    usleep (pingdelay_ms * 1000);
                }
                break;
            }
            case 'b': { /* --barrier NAME */
                if (cmb_barrier (c, optarg, nprocs, tasks_per_node) < 0) {
                    fprintf (stderr, "cmb_barrier: %s\n", strerror(errno));
                    exit (1);
                }
                break;
            }
            case 's': { /* --snoop */
                if (cmb_snoop (c, "") < 0) {
                    fprintf (stderr, "cmb_snoop: %s\n", strerror(errno));
                    exit (1);
                }
                break;
            }
            case 'S': { /* --sync */
                if (cmb_sync (c) < 0) {
                    fprintf (stderr, "cmb_sync: %s\n", strerror(errno));
                    exit (1);
                }
                break;
            }
#if 0
            case 'f': { /* --flood packetsize */
                if (cmb_flood (c, strtoul (optarg, NULL, 10)) < 0) {
                    fprintf (stderr, "cmb_flood: %s\n", strerror(errno));
                    exit (1);
                }
                break;
            }
#endif
            case 'k': { /* --kvs-put key=val */
                char *key = optarg;
                char *val = strchr (optarg, '=');
                if (val == NULL)
                    usage ();
                *val++ = '\0';
                if (cmb_kvs_put (c, key, val) < 0) {
                    fprintf (stderr, "cmb_kvs_put: %s\n", strerror(errno));
                    exit (1);
                }
                break;
            }
            case 'K': { /* --kvs-get key */
                char *val = cmb_kvs_get (c, optarg);
                if (!val && errno != 0) {
                    fprintf (stderr, "cmb_kvs_get: %s\n", strerror(errno));
                    exit (1);
                }
                printf ("%s=%s\n", optarg, val ? val : "<nil>");
                if (val)
                    free (val);
                break;
            }
            case 'C': { /* --kvs-commit */
                if (cmb_kvs_commit (c) < 0) {
                    fprintf (stderr, "cmb_kvs_commit: %s\n", strerror(errno));
                    exit (1);
                }
                break;
            }
            case 't': { /* --kvs-torture N */
                int i, n = strtoul (optarg, NULL, 10);
                char key[16], val[16];
                struct timeval t1, t2, t;

                gettimeofday (&t1, NULL);
                for (i = 0; i < n; i++) {
                    snprintf (key, sizeof (key), "key%d", i);
                    snprintf (val, sizeof (key), "val%d", i);
                    if (cmb_kvs_put (c, key, val) < 0) {
                        fprintf (stderr, "cmb_kvs_put: %s\n", strerror(errno));
                        exit (1);
                    }
                }
                gettimeofday (&t2, NULL);
                timersub(&t2, &t1, &t);
                fprintf (stderr, "kvs_put:    time=%0.3f ms\n",
                        (double)t.tv_sec * 1000 + (double)t.tv_usec / 1000);
                if (cmb_kvs_commit (c) < 0) {
                    fprintf (stderr, "cmb_kvs_commit: %s\n", strerror(errno));
                    exit (1);
                }
                gettimeofday (&t2, NULL);
                timersub (&t2, &t1, &t);
                fprintf (stderr, "kvs_commit: time=%0.3f ms\n",
                        (double)t.tv_sec * 1000 + (double)t.tv_usec / 1000);
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

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "cmb.h"

#define OPTIONS "psb:f:k:SK:C"
static const struct option longopts[] = {
    {"ping",       no_argument,        0, 'p'},
    {"snoop",      no_argument,        0, 's'},
    {"barrier",    required_argument,  0, 'b'},
    {"flood",      required_argument,  0, 'f'},
    {"kvs-put",    required_argument,  0, 'k'},
    {"kvs-get",    required_argument,  0, 'K'},
    {"kvs-commit", no_argument,        0, 'C'},
    {"sync",       no_argument,        0, 'S'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr, "Usage: cmbutil [-p|-s sub|-b name|-f size|-k key=val]\n");
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

    nprocs = _env_getint ("SLURM_NPROCS", 1);
    tasks_per_node = _env_getint ("SLURM_TASKS_PER_NODE", 1);

    if (!(c = cmb_init ())) {
        fprintf (stderr, "cmb_init: %s\n", strerror (errno));
        exit (1);
    }
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'p': { /* --ping */
                int i;
                for (i = 0; ; i++) {
                    if (cmb_ping (c, i) < 0) {
                        fprintf (stderr, "cmb_ping: %s\n", strerror(errno));
                        exit (1);
                    }
                    sleep (1);
                }
                break;
            }
            case 'b': { /* --barrier NAME */
                if (cmb_barrier (c, optarg, 1, nprocs, tasks_per_node) < 0) {
                    fprintf (stderr, "cmb_barrier: %s\n", strerror(errno));
                    exit (1);
                }
                break;
            }
            case 's': { /* --snoop */
                if (cmb_snoop (c, NULL) < 0) {
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
                if (!val) {
                    fprintf (stderr, "cmb_kvs_get: %s\n", strerror(errno));
                    exit (1);
                }
                printf ("%s=%s\n", optarg, val);
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

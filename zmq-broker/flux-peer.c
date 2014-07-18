/* flux-peer.c - flux peer subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"
#include "shortjson.h"

#define OPTIONS "+hr:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: flux-peer [--rank N] idle\n"
"       flux-peer [--rank N] parent-uri\n"
"       flux-peer [--rank N] request-uri\n"
"       flux-peer [--rank N] reparent new-parent-uri\n"
"       flux-peer [--rank N] panic [msg ...]\n"
"       flux-peer [--rank N] failover\n"
"       flux-peer [--rank N] recover\n"
"       flux-peer            allrecover\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    int rank = -1; /* local */
    char *cmd;

    log_init ("flux-peer");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'r': /* --rank N */
                rank = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    cmd = argv[optind++];

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (!strcmp (cmd, "reparent")) {
        if (optind != argc - 1)
            usage ();
        if (flux_reparent (h, rank, argv[optind]) < 0)
            err_exit ("flux_reparent");
    } else if (!strcmp (cmd, "idle")) {
        if (optind != argc)
            usage ();
        JSON peers;
        if (!(peers = flux_lspeer (h, rank)))
            err_exit ("flux_peer");
        printf ("%s\n", Jtostr (peers));
        Jput (peers);
    } else if (!strcmp (cmd, "parent-uri")) {
        if (optind != argc)
            usage ();
        char *s;
        if (!(s = flux_getattr (h, rank, "cmbd-parent-uri")))
            err_exit ("flux_getattr cmbd-parent-uri");
        printf ("%s\n", s);
        free (s);
    } else if (!strcmp (cmd, "request-uri")) {
        if (optind != argc)
            usage ();
        char *s;
        if (!(s = flux_getattr (h, rank, "cmbd-request-uri")))
            err_exit ("flux_getattr cmbd-request-uri");
        printf ("%s\n", s);
        free (s);
    } else if (!strcmp (cmd, "panic")) {
        char *msg = NULL;
        if (optind < argc)
            msg = argv_concat (argc - optind, argv + optind);
        flux_panic (h, rank, msg);
        if (msg)
            free (msg);
    } else if (!strcmp (cmd, "failover")) {
        if (optind != argc)
            usage ();
        if (flux_failover (h, rank) < 0)
            err_exit ("flux_failover");
    } else if (!strcmp (cmd, "recover")) {
        if (optind != argc)
            usage ();
        if (flux_recover (h, rank) < 0)
            err_exit ("flux_recover");
    } else if (!strcmp (cmd, "allrecover")) {
        if (optind != argc)
            usage ();
        if (flux_recover_all (h) < 0)
            err_exit ("flux_recover_all");
    } else
        usage ();

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

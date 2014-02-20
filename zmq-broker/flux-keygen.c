/* flux-keygen.c - flux key management subcommand */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>
#include <zmq.h>
#include <czmq.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#if ZMQ_VERSION_MAJOR >= 4
#define HAVE_CURVE_SECURITY 1
#endif

#if HAVE_CURVE_SECURITY
#include "curve.h"
#endif

#define OPTIONS "hfn:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"force",      no_argument,        0, 'f'},
    {"name",       required_argument,  0, 'n'},
    { 0, 0, 0, 0 },
};

char * ctime_iso8601_now (char *buf, size_t sz);

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-keygen [--force] [--name NAME]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    char *name = "flux";
    bool fopt = false;
    int ch;

    log_init ("flux-keygen");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'f': /* --force */
                fopt = true;
                break;
            case 'n': /* --name */
                name = optarg;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind < argc)
        usage ();
#if HAVE_CURVE_SECURITY
    char *dir;
    if (!(dir = flux_curve_getpath ()) || flux_curve_checkpath (dir, true) < 0)
        exit (1);
    if (flux_curve_gencli (dir, name, fopt) < 0 || flux_curve_gensrv (dir,
                                                             name, fopt) < 0) {
        if (!fopt)
            fprintf (stderr, "Use --force to unlink before regen.\n"); 
        exit (1);
    }

    /* Try loading keys just to be sure they work.
     */
    zcert_t *cert;
    if (!(cert = flux_curve_getcli (dir, name)))
        exit (1);
    zcert_destroy (&cert);
    if (!(cert = flux_curve_getsrv (dir, name)))
        exit (1);
    zcert_destroy (&cert);
    free (dir);
#else
    msg_exit ("Flux was built without support for CURVE security");
#endif


    log_fini ();
    
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

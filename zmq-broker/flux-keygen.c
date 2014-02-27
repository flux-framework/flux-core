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
#include "security.h"

#define OPTIONS "hfp"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"force",      no_argument,        0, 'f'},
    {"plain",      no_argument,        0, 'p'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-keygen [--force] [--plain|--curve]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    flux_sec_t sec;
    bool force = false;
    bool plain = false;
    bool curve = false;

    log_init ("flux-keygen");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'f': /* --force */
                force = true;
                break;
            case 'p': /* --plain */
                plain = true;
                break;
            case 'c': /* --curve */
                curve = true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind < argc)
        usage ();
    if (plain && curve)
        usage ();
    if (!(sec = flux_sec_create ()))
        err_exit ("flux_sec_create");
    if (plain && flux_sec_enable (sec, FLUX_SEC_TYPE_PLAIN) < 0)
        msg_exit ("PLAIN security is not available");
    if (curve && flux_sec_enable (sec, FLUX_SEC_TYPE_CURVE) < 0)
        msg_exit ("CURVE security is not available");
    if (flux_sec_keygen (sec, force, true) < 0)
        msg_exit ("%s", flux_sec_errstr (sec));
    flux_sec_destroy (sec);

    log_fini ();
    
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

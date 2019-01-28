/* setrootevents.c - pause / unpause setroot event reception */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <getopt.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"

#define OPTIONS "hpun:"
static const struct option longopts[] = {
    {"help",      no_argument,       0, 'h'},
    {"pause",     no_argument,       0, 'p'},
    {"unpause",   no_argument,       0, 'u'},
    {"namespace", required_argument, 0, 'n'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: setrootevents --pause\n"
"       or\n"
"       setrootevents --unpause\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    int ch;
    bool pause = false;
    bool unpause = false;
    const char *ns = KVS_PRIMARY_NAMESPACE;
    char *topic;
    flux_future_t *f;

    log_init ("setrootevents");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'p': /* --pause */
                pause = true;
                break;
            case 'u': /* --unpause */
                unpause = true;
                break;
            case 'n': /* --namespace */
                ns = optarg;
                break;
            default:
                usage ();
                break;
        }
    }
    if ((!pause && !unpause)
        || (pause && unpause))
        usage();

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (pause)
        topic = "kvs.setroot-pause";
    else
        topic = "kvs.setroot-unpause";

    if (!(f = flux_rpc_pack (h, topic, FLUX_NODEID_ANY, 0,
                             "{ s:s }",
                             "namespace", ns)))
        log_err_exit ("flux_rpc_pack");

    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_future_get");

    flux_close (h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

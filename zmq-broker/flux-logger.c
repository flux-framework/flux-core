/* flux-logger.c - flux logger subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "zmsg.h"
#include "log.h"

#define OPTIONS "hp:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"priority",   required_argument,  0, 'p'},
    { 0, 0, 0, 0 },
};

int parse_logstr (char *s, int *lp, char **fp);

void usage (void)
{
    fprintf (stderr, "Usage: flux-logger [--priority facility.level] message ...\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *message = NULL;
    char *priority = "user.notice";
    int level;
    char *facility;

    log_init ("flux-logger");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'p': /* --priority facility.level */
                priority = optarg;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    message = argv_concat (argc - optind, argv + optind);

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (parse_logstr (priority, &level, &facility) < 0)
        msg_exit ("bad priority argument");
    flux_log_set_facility (h, facility);
    if (flux_log (h, level, "%s", message) < 0)
        err_exit ("cmb_log");

    flux_handle_destroy (&h);

    free (message);
    free (facility);
    log_fini ();
    return 0;
}

int parse_logstr (char *s, int *lp, char **fp)
{
    char *p, *fac = xstrdup (s);
    int lev = LOG_INFO;

    if ((p = strchr (fac, '.'))) {
        *p++ = '\0';
        lev = log_strtolevel (p);
        if (lev < 0)
            return -1;
    }
    *lp = lev;
    *fp = fac;
    return 0;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

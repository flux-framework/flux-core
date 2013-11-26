/* flux-log.c - flux log subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "zmsg.h"
#include "log.h"

#define OPTIONS "hp:wd"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"watch",      no_argument,        0, 'w'},
    {"dump",       no_argument,        0, 'd'},
    {"priority",   required_argument,  0, 'p'},
    { 0, 0, 0, 0 },
};

int parse_logstr (char *s, int *lp, char **fp);
void dump_log (flux_t h);

void usage (void)
{
    fprintf (stderr, "Usage: flux-log [--watch|dump] [--priority facility.level]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *priority = "user.notice";
    int level;
    char *facility;
    bool watch = false;
    bool dump = false;

    log_init ("flux-log");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'p': /* --priority facility.level */
                priority = optarg;
                break;
            case 'w': /* --watch */
                watch = true;
                break;
            case 'd': /* --dump */
                dump = true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc)
        usage ();
    if (!watch && !dump)
        usage();

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (parse_logstr (priority, &level, &facility) < 0)
        msg_exit ("bad priority argument");

    if (watch) {
        if (flux_log_subscribe (h, level, facility) < 0)
            err_exit ("flux_log_subscribe");
    } else if (dump) {
        if (flux_log_dump (h, level, facility) < 0)
            err_exit ("flux_log_dump");
    }
    dump_log (h);

    flux_handle_destroy (&h);

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

/* Dump all flux_log responses ot stderr.
 * In the case of a log subscription, will run forever.
 * In the case of a log dump, will terminate when logsrv responds with ENOENT.
 */
void dump_log (flux_t h)
{
    char *src, *fac, *s;
    struct timeval tv, start = { .tv_sec = 0 }, rel;
    int count, lev;
    zmsg_t *zmsg;
    const char *levstr;

    while ((zmsg = flux_response_recvmsg (h, false))) {
        if (!(s = flux_log_decode (zmsg, &lev, &fac, &count, &tv, &src))) {
            if (errno != ENOENT)
                err ("flux_log_decode");
            zmsg_destroy (&zmsg);
            return;
        }
        if (start.tv_sec == 0)
            start = tv;
        timersub (&tv, &start, &rel);
        levstr = log_leveltostr (lev);
        //printf ("XXX lev=%d (%s)\n", lev, levstr);
        fprintf (stderr, "[%-.6lu.%-.6lu] %dx %s.%s[%s]: %s\n",
                 rel.tv_sec, rel.tv_usec, count,
                 fac, levstr ? levstr : "unknown", src, s);
        zmsg_destroy (&zmsg);
        free (fac);
        free (src);
        free (s);
    }
    err ("flux_response_recvmsg");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

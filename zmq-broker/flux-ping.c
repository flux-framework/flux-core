/* flux-ping.c - flux ping subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "hp:d:r:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    {"pad-bytes",  required_argument,  0, 'p'},
    {"delay-msec", required_argument,  0, 'd'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-ping [--rank N] [--pad-bytes N] [--delay-msec N] [node!]tag\n"
);
    exit (1);
}

static char *ping (flux_t h, int rank, const char *name, const char *pad,
                   int seq);

int main (int argc, char *argv[])
{
    flux_t h;
    int ch, seq, bytes = 0, msec = 1000, rank = -1;
    char *rankstr = NULL, *route, *target, *pad = NULL;
    struct timespec t0;

    log_init ("flux-ping");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'p': /* --pad-bytes N */
                bytes = strtoul (optarg, NULL, 10);
                pad = xzmalloc (bytes + 1);
                memset (pad, 'p', bytes);
                break;
            case 'd': /* --delay-msec N */
                msec = strtoul (optarg, NULL, 10);
                break;
            case 'r': /* --rank N */
                rank = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc - 1)
        usage ();
    target = argv[optind++];

    if (rank == -1) {
        char *endptr;
        int n = strtoul (target, &endptr, 10);
        if (endptr != target)
            rank = n;
        if (*endptr == '!')
            target = endptr + 1;
        else
            target = endptr;
    }
    if (*target == '\0')
        target = "cmb";
    if (rank != -1) {
        if (asprintf (&rankstr, "%d", rank) < 0)
            oom ();
    }

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    for (seq = 0; ; seq++) {
        monotime (&t0);
        if (!(route = ping (h, rank, target, pad, seq)))
            err_exit ("%s%s%s.ping", rank == -1 ? "" : rankstr,
                                     rank == -1 ? "" : "!", target);
        printf ("%s%s%s.ping pad=%d seq=%d time=%0.3f ms (%s)\n",
                rankstr ? rankstr : "",
                rankstr ? "!" : "",
                target, bytes, seq, monotime_since (t0), route);
        free (route);
        usleep (msec * 1000);
    }

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/* Ping plugin 'name'.
 * 'pad' is a string used to increase the size of the ping packet for
 * measuring RTT in comparison to rough message size.
 * 'seq' is a sequence number.
 * The pad and seq are echoed in the response, and any mismatch will result
 * in an error return with errno = EPROTO.
 * A string representation of the route taken is the return value on success
 * (caller must free).  On error, return NULL with errno set.
 */
static char *ping (flux_t h, int rank, const char *name, const char *pad,
                        int seq)
{
    json_object *request = util_json_object_new_object ();
    json_object *response = NULL;
    int rseq;
    const char *route, *rpad;
    char *ret = NULL;

    if (pad)
        util_json_object_add_string (request, "pad", pad);
    util_json_object_add_int (request, "seq", seq);

    if (!(response = flux_rank_rpc (h, rank, request, "%s.ping", name)))
        goto done;
    if (util_json_object_get_int (response, "seq", &rseq) < 0
            || util_json_object_get_string (response, "route", &route) < 0) {
        errno = EPROTO;
        goto done;
    }
    if (seq != rseq) {
        msg ("%s: seq not echoed back", __FUNCTION__);
        errno = EPROTO;
        goto done;
    }
    if (pad) {
        if (util_json_object_get_string (response, "pad", &rpad) < 0
                                || !rpad || strlen (pad) != strlen (rpad)) {
            msg ("%s: pad not echoed back", __FUNCTION__);
            errno = EPROTO;
            goto done;
        }
    }
    ret = strdup (route);
done:
    if (response)
        json_object_put (response);
    if (request)
        json_object_put (request);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

/* flux-host.c - list hostname information for rank */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "hostlist.h"
#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "ha"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"address",    no_argument,        0, 'a'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-host [--address] rank ...\n"
);
    exit (1);
}

static char *rank2host (json_object *hosts, int rank);
static char *rank2addr (json_object *hosts, int rank);

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    json_object *hosts;
    hostlist_t hl;
    hostlist_iterator_t itr;
    char *host;
    bool aopt = false;

    log_init ("flux-host");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'a':   /* --address */
                aopt = true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (!(h = cmb_init ()))
        err_exit ("cmb_init");
    if (kvs_get (h, "hosts", &hosts) < 0)
        err_exit ("kvs_get hosts");

    if (!(hl = hostlist_create ("")))
        oom ();
    if (optind == argc) {
        int size;
        char *s;
        if (flux_info (h, NULL, &size, NULL) < 0)
            err_exit ("flux_info");
        if (asprintf (&s, "0-%d", size - 1) < 0)
            oom ();
        if (!hostlist_push (hl, s))
            err_exit ("hostlist_push: %s", s);
        free (s);
    } else {
        for (; optind < argc; optind++) {
            if (!hostlist_push (hl, argv[optind]))
                err_exit ("hostlist_push: %s", argv[optind]);
        }
    }
    hostlist_sort (hl);
    hostlist_uniq (hl);

    if (!(itr = hostlist_iterator_create (hl)))
        oom ();
    while ((host = hostlist_next (itr))) {
        int rank = strtoul (host, NULL, 10);
        char *s = aopt ? rank2addr (hosts, rank) : rank2host (hosts, rank);
        printf ("%d:\t%s\n", rank, s);
        free (s);
    }
    hostlist_iterator_destroy (itr);

    hostlist_destroy (hl);
    json_object_put (hosts);
    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

static char *rank2host (json_object *hosts, int rank)
{
    json_object *o;
    const char *name;

    if (!(o = json_object_array_get_idx (hosts, rank)))
        msg_exit ("%s: rank %d not found", __FUNCTION__, rank);
    if (util_json_object_get_string (o, "name", &name) < 0)
        msg_exit ("%s: rank %d malformed hosts entry", __FUNCTION__, rank);
    return xstrdup (name);
}

static char *rank2addr (json_object *hosts, int rank)
{
    json_object *o, *a, *a0;
    const char *addr;

    if (!(o = json_object_array_get_idx (hosts, rank)))
        msg_exit ("%s: rank %d not found", __FUNCTION__, rank);
    if (!(a = json_object_object_get (o, "addrs"))
                || !(a0 = json_object_array_get_idx (a, 0))
                || !(addr = json_object_get_string (a0)))
        msg_exit ("%s: rank %d no addr", __FUNCTION__, rank);
    return xstrdup (addr);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

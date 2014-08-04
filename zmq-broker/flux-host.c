/* flux-host.c - list hostname information for rank */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>
#include <stdbool.h>

#include "nodeset.h"
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
"Usage: flux-host [--address] [nodeset] ...\n"
);
    exit (1);
}

static const char *rank2host (json_object *hosts, uint32_t rank);
static const char *rank2addr (json_object *hosts, uint32_t rank);

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    json_object *hosts;
    nodeset_t n = nodeset_new ();
    nodeset_itr_t itr;
    uint32_t r;
    int size;
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
    if ((size = flux_size (h)) < 0)
        err_exit ("flux_size");
    if (kvs_get (h, "hosts", &hosts) < 0)
        err_exit ("kvs_get hosts");

    if (optind == argc) {
        nodeset_add_range (n, 0, size - 1);
    } else {
        while (optind < argc) {
            if (!nodeset_add_str (n, argv[optind++]))
                err_exit ("error parsing nodeset argument");
        }
    }

    itr = nodeset_itr_new (n);
    while ((r = nodeset_next (itr)) != NODESET_EOF) {
        printf ("%u:\t%s\n", r, aopt ? rank2addr (hosts, r)
                                     : rank2host (hosts, r));
    }
    nodeset_itr_destroy (itr);
    nodeset_destroy (n);

    json_object_put (hosts);
    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

static const char *rank2host (json_object *hosts, uint32_t rank)
{
    json_object *o;
    const char *name;

    if (!(o = json_object_array_get_idx (hosts, rank)))
        msg_exit ("%s: rank %d not found", __FUNCTION__, rank);
    if (util_json_object_get_string (o, "name", &name) < 0)
        msg_exit ("%s: rank %d malformed hosts entry", __FUNCTION__, rank);
    return name;
}

static const char *rank2addr (json_object *hosts, uint32_t rank)
{
    json_object *o, *a, *a0;
    const char *addr;

    if (!(o = json_object_array_get_idx (hosts, rank)))
        msg_exit ("%s: rank %d not found", __FUNCTION__, rank);
    if (!(a = json_object_object_get (o, "addrs"))
                || !(a0 = json_object_array_get_idx (a, 0))
                || !(addr = json_object_get_string (a0)))
        msg_exit ("%s: rank %d no addr", __FUNCTION__, rank);
    return addr;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

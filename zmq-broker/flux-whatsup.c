/* flux-whatsup.c - list node status */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"
#include "hostlist.h"

#define OPTIONS "hduH"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"down",       no_argument,        0, 'd'},
    {"up",         no_argument,        0, 'u'},
    {"hostname",   no_argument,        0, 'H'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-whatsup\n"
);
    exit (1);
}

static char *rank2str (int rank);
static char *rank2host (json_object *hosts, int rank);

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    json_object *o, *dn = NULL;
    json_object *hosts;
    hostlist_t dnhl, uphl;
    char *s, buf[256];
    int i, len;
    bool dopt = false;
    bool uopt = false;
    bool Hopt = false;

    log_init ("flux-whatsup");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'H': /* --hostname */
                Hopt = true;
                break;
            case 'd': /* --down */
                dopt = true;
                break;
            case 'u': /* --up */
                uopt = true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc)
        usage ();

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (Hopt) {
        if (kvs_get (h, "hosts", &hosts) < 0)
            err_exit ("kvs_get hosts");
    }

    /* Convert json array in KVS to hostlist for DOWN nodes.
     */
    if (!(dnhl = hostlist_create ("")))
        oom ();
    if (kvs_get (h, "conf.live.down", &dn) < 0 && errno != ENOENT)
        err_exit ("kvs_get");
    if (dn) {
        len = json_object_array_length (dn);
        for (i = 0; i < len; i++) {
            o = json_object_array_get_idx (dn, i);
            if (Hopt)
                s = rank2host (hosts, json_object_get_int (o));
            else
                s = rank2str (json_object_get_int (o));
            hostlist_push_host (dnhl, s);
            free (s);
            json_object_put (o);
        }
        json_object_put (dn);
    }

    /* Create hostlist for UP nodes from session size and dnhl.
     */
    if (uopt || !dopt) {
        if (!(uphl = hostlist_create ("")))
            oom ();
        if (flux_info (h, NULL, &len, NULL) < 0)
            err_exit ("flux_info");
        for (i = 0; i < len; i++) {
            if (Hopt)
                s = rank2host (hosts, i);
            else
                s = rank2str (i);
            if (hostlist_find (dnhl, s) == -1)
                hostlist_push_host (uphl, s); 
            free (s);
        }
        hostlist_sort (uphl);
        hostlist_uniq (uphl);
        hostlist_ranged_string (uphl, sizeof (buf), buf);
        printf ("up:   %s\n", buf);
        hostlist_destroy (uphl);
    }

    if (dopt || !uopt) {
        hostlist_sort (dnhl);
        hostlist_uniq (dnhl);
        hostlist_ranged_string (dnhl, sizeof (buf), buf);
        if (dopt || !uopt)
            printf ("down: %s\n", buf);
    }

    hostlist_destroy (dnhl);
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

static char *rank2str (int rank)
{
    char *s;
    if (asprintf (&s, "%d", rank) < 0)
        oom ();
    return s;
}
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

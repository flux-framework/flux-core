/* flux-up.c - list node status */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"
#include "hostlist.h"

#define OPTIONS "hduHcn"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"down",       no_argument,        0, 'd'},
    {"up",         no_argument,        0, 'u'},
    {"hostname",   no_argument,        0, 'H'},
    {"comma",      no_argument,        0, 'c'},
    {"newline",    no_argument,        0, 'n'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-up [--up|--down] [--hostname] [--comma|--newline] [nodelist]\n"
);
    exit (1);
}

#define CHUNK_SIZE 80
typedef enum {
    HLSTR_COMMA,
    HLSTR_NEWLINE,
    HLSTR_RANGED,
} hlstr_type_t;

static char *rank2str (int rank);
static char *rank2host (json_object *hosts, int rank);
static char *hostlist_tostring (hostlist_t hl, hlstr_type_t type);

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    json_object *o, *dn = NULL;
    json_object *hosts;
    hostlist_t dnhl, uphl;
    char *s;
    int i, len;
    bool dopt = false;
    bool uopt = false;
    bool Hopt = false;
    bool copt = false;
    bool nopt = false;

    log_init ("flux-up");

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
            case 'c': /* --comma */
                copt = true;
                break;
            case 'n': /* --newline */
                nopt = true;
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
        if (nopt)
            s = hostlist_tostring (uphl, HLSTR_NEWLINE);
        else if (copt)
            s = hostlist_tostring (uphl, HLSTR_COMMA);
        else
            s = hostlist_tostring (uphl, HLSTR_RANGED);
        printf ("%s%s%s", uopt ? "" : "up:\t", s,
                uopt && strlen (s) == 0 ? "" : "\n");
        hostlist_destroy (uphl);
        free (s);
    }

    if (dopt || !uopt) {
        if (nopt)
            s = hostlist_tostring (dnhl, HLSTR_NEWLINE);
        else if (copt)
            s = hostlist_tostring (dnhl, HLSTR_COMMA);
        else
            s = hostlist_tostring (dnhl, HLSTR_RANGED);
        printf ("%s%s%s", dopt ? "" : "down:\t", s,
                dopt && strlen (s) == 0 ? "" : "\n");
        free (s);
    }

    hostlist_destroy (dnhl);
    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

static char *hostlist_tostring (hostlist_t hl, hlstr_type_t type)
{
    int len = CHUNK_SIZE;
    char *buf = xzmalloc (len);

    hostlist_sort (hl);
    hostlist_uniq (hl);

    switch (type) {
        case HLSTR_COMMA:
            while (hostlist_deranged_string (hl, len, buf) < 0)
                if (!(buf = realloc (buf, len += CHUNK_SIZE)))
                    oom ();
            break;
        case HLSTR_RANGED:
            while (hostlist_ranged_string (hl, len, buf) < 0)
                if (!(buf = realloc (buf, len += CHUNK_SIZE)))
                    oom ();
            break;
        case HLSTR_NEWLINE: {
            hostlist_iterator_t itr;
            char *host;

            if (!(itr = hostlist_iterator_create (hl)))
                oom ();
            while ((host = hostlist_next (itr))) {
                while (len - strlen (buf) < strlen (host) + 2) {
                    if (!(buf = realloc (buf, len += CHUNK_SIZE)))
                        oom ();
                }
                snprintf (buf + strlen (buf), len - strlen (buf), "%s\n", host);
            }
            hostlist_iterator_destroy (itr);
            break;
        }
    }
    if (buf[strlen (buf) - 1] == '\n')
        buf[strlen (buf) - 1] = '\0';
    return buf;
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

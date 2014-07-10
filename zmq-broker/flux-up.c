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
#include "shortjson.h"

#define OPTIONS "hHcnud"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"hostname",   no_argument,        0, 'H'},
    {"comma",      no_argument,        0, 'c'},
    {"newline",    no_argument,        0, 'n'},
    {"up",         no_argument,        0, 'u'},
    {"down",       no_argument,        0, 'd'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-up [OPTIONS]\n"
"where options are:\n"
"  -H,--hostname    print hostnames instead of ranks\n"
"  -c,--comma       print commas instead of ranges\n"
"  -n,--newline     print newlines instead of ranges\n"
"  -u,--up          print only nodes in ok or slow state\n"
"  -d,--down        print only nodes in fail state\n"
);
    exit (1);
}

#define CHUNK_SIZE 80
typedef enum {
    HLSTR_COMMA,
    HLSTR_NEWLINE,
    HLSTR_RANGED,
} hlstr_type_t;

typedef struct {
    hostlist_t ok;
    hostlist_t fail;
    hostlist_t slow;
    hostlist_t unknown;
} ns_t;

static char *rank2host (JSON hosts, const char *rankstr);
static char *hostlist_tostring (hostlist_t hl, hlstr_type_t type);

static ns_t *ns_fromkvs (flux_t h);
static void ns_tohost (ns_t *ns, JSON hosts);
static void ns_print (ns_t *ns, hlstr_type_t fmt);
static void ns_destroy (ns_t *ns);
static void ns_print_down (ns_t *ns, hlstr_type_t fmt);
static void ns_print_up (ns_t *ns, hlstr_type_t fmt);

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    bool Hopt = false;
    bool uopt = false;
    bool dopt = false;
    hlstr_type_t fmt = HLSTR_RANGED;
    ns_t *ns;

    log_init ("flux-up");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'H': /* --hostname */
                Hopt = true;
                break;
            case 'c': /* --comma */
                fmt = HLSTR_COMMA;
                break;
            case 'n': /* --newline */
                fmt = HLSTR_NEWLINE;
                break;
            case 'u': /* --up */
                uopt = true;
                break;
            case 'd': /* --down */
                dopt = true;
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

    if (!(ns = ns_fromkvs (h)))
        err_exit ("conf.live.status");
    if (Hopt) {
        JSON hosts;
        if (kvs_get (h, "hosts", &hosts) < 0)
            err_exit ("kvs_get hosts");
        ns_tohost (ns, hosts);
        Jput (hosts);
    }
    if (dopt)
        ns_print_down (ns, fmt);
    else if (uopt)
        ns_print_up (ns, fmt);
    else
        ns_print (ns, fmt);
    ns_destroy (ns);

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

static char *rank2host (JSON hosts, const char *rankstr)
{
    JSON o;
    const char *name;
    int rank = strtoul (rankstr, NULL, 10);

    if (!Jget_ar_obj (hosts, rank, &o))
        msg_exit ("%s: rank %d not found in hosts", __FUNCTION__, rank);
    if (!Jget_str (o, "name", &name))
        msg_exit ("%s: rank %d malformed hosts entry", __FUNCTION__, rank);
    return xstrdup (name);
}

static bool Jget_hl (JSON o, const char *name, hostlist_t *hp)
{
    hostlist_t hl;
    const char *s;

    if (!Jget_str (o, name, &s) || !(hl = hostlist_create (s)))
        return false;
    *hp = hl;
    return true;
}

static ns_t *ns_fromjson (JSON o)
{
    ns_t *ns = xzmalloc (sizeof (*ns));

    if (!Jget_hl (o, "ok", &ns->ok) || !Jget_hl (o, "unknown", &ns->unknown)
     || !Jget_hl (o, "slow", &ns->slow) || !Jget_hl (o, "fail", &ns->fail)) {
        ns_destroy (ns);
        return NULL;
    }
    return ns;
}

static ns_t *ns_fromkvs (flux_t h)
{
    JSON o = NULL;
    ns_t *ns = NULL;

    if (kvs_get (h, "conf.live.status", &o) < 0)
        goto done;
    ns = ns_fromjson (o);
done:
    Jput (o);
    return ns;
}

static void ns_destroy (ns_t *ns)
{
    hostlist_destroy (ns->ok);
    hostlist_destroy (ns->slow);
    hostlist_destroy (ns->unknown);
    hostlist_destroy (ns->fail);
    free (ns);
}

static hostlist_t nl_tohost (hostlist_t hl, JSON hosts)
{
    hostlist_t nhl = hostlist_create ("");
    hostlist_iterator_t itr;
    char *s;

    if (!(itr = hostlist_iterator_create (hl)))
        oom ();
    while ((s = hostlist_next (itr))) {
        char *h = rank2host (hosts, s);
        hostlist_push_host (nhl, h);
        free (h);
    }
    hostlist_iterator_destroy (itr);
    hostlist_destroy (hl);
    return nhl;
}

static void ns_tohost (ns_t *ns, JSON hosts)
{
    ns->ok = nl_tohost (ns->ok, hosts);
    ns->slow = nl_tohost (ns->slow, hosts);
    ns->fail = nl_tohost (ns->fail, hosts);
    ns->unknown = nl_tohost (ns->unknown, hosts);
}

static void nl_print (hostlist_t hl, const char *label, hlstr_type_t fmt)
{
    char *s = hostlist_tostring (hl, fmt);

    if (label) {
        if (fmt == HLSTR_NEWLINE)
            printf ("%-8s\n%s%s", label, s, strlen (s) > 0 ? "\n" : "");
        else
            printf ("%-8s%s\n", label, s);
    } else {
        if (fmt == HLSTR_NEWLINE)
            printf ("%s%s", s, strlen (s) > 0 ? "\n" : "");
        else
            printf ("%s\n", s);
    }
    free (s);
}

static void ns_print (ns_t *ns, hlstr_type_t fmt)
{
    nl_print (ns->ok, "ok:", fmt);
    nl_print (ns->slow, "slow:", fmt);
    nl_print (ns->fail, "fail:", fmt);
    nl_print (ns->unknown, "unknown:", fmt);
}

static void ns_print_up (ns_t *ns, hlstr_type_t fmt)
{
    hostlist_t hl = hostlist_copy (ns->ok);
    if (!hl)
        oom ();
    (void)hostlist_push_list (hl, ns->slow);
    nl_print (hl, NULL, fmt);
    hostlist_destroy (hl);
}

static void ns_print_down (ns_t *ns, hlstr_type_t fmt)
{
    nl_print (ns->fail, NULL, fmt);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

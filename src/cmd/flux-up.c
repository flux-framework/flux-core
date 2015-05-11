/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <getopt.h>
#include <stdbool.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libutil/shortjson.h"


#define OPTIONS "hcnud"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
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
"  -c,--comma       print commas instead of ranges\n"
"  -n,--newline     print newlines instead of ranges\n"
"  -u,--up          print only nodes in ok or slow state\n"
"  -d,--down        print only nodes in fail state\n"
);
    exit (1);
}

#define CHUNK_SIZE 80
typedef enum {
    FMT_COMMA,
    FMT_NEWLINE,
    FMT_RANGED,
} fmt_t;

typedef struct {
    nodeset_t ok;
    nodeset_t fail;
    nodeset_t slow;
    nodeset_t unknown;
} ns_t;

static ns_t *ns_guess (flux_t h);
static ns_t *ns_fromkvs (flux_t h);
static void ns_destroy (ns_t *ns);
static void ns_print_down (ns_t *ns, fmt_t fmt);
static void ns_print_up (ns_t *ns, fmt_t fmt);
static void ns_print_all (ns_t *ns, fmt_t fmt);

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    bool uopt = false;
    bool dopt = false;
    fmt_t fmt = FMT_RANGED;
    ns_t *ns;

    log_init ("flux-up");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'c': /* --comma */
                fmt = FMT_COMMA;
                break;
            case 'n': /* --newline */
                fmt = FMT_NEWLINE;
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

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");

    if (!(ns = ns_fromkvs (h)))
        ns = ns_guess (h);
    if (dopt)
        ns_print_down (ns, fmt);
    else if (uopt)
        ns_print_up (ns, fmt);
    else
        ns_print_all (ns, fmt);
    ns_destroy (ns);

    flux_close (h);
    log_fini ();
    return 0;
}

static bool Jget_nodeset (JSON o, const char *name, nodeset_t *np)
{
    nodeset_t ns;
    const char *s;

    if (!Jget_str (o, name, &s) || !(ns = nodeset_new_str (s)))
        return false;
    *np = ns;
    return true;
}

static ns_t *ns_fromjson (JSON o)
{
    ns_t *ns = xzmalloc (sizeof (*ns));

    if (!Jget_nodeset (o, "ok", &ns->ok)
                || !Jget_nodeset (o, "unknown", &ns->unknown)
                || !Jget_nodeset (o, "slow", &ns->slow)
                || !Jget_nodeset (o, "fail", &ns->fail)) {
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

static ns_t *ns_guess (flux_t h)
{
    ns_t *ns = xzmalloc (sizeof (*ns));
    int size, rank;
    bool treeroot;
    uint32_t r;

    if (flux_info (h, &rank, &size, &treeroot) < 0)
        err_exit ("flux_info");
    ns->ok = nodeset_new ();
    ns->slow = nodeset_new ();
    ns->fail = nodeset_new ();
    ns->unknown = nodeset_new ();
    if (!ns->ok || !ns->slow || !ns->fail || !ns->unknown)
        oom ();

    nodeset_add_rank (ns->ok, rank);
    for (r = 0; r < size; r++) {
        if (r != rank)
            nodeset_add_rank (ns->unknown, r);
    }
    return ns;
}

static void ns_destroy (ns_t *ns)
{
    nodeset_destroy (ns->ok);
    nodeset_destroy (ns->slow);
    nodeset_destroy (ns->unknown);
    nodeset_destroy (ns->fail);
    free (ns);
}

static void nodeset_print (nodeset_t ns, const char *label, fmt_t fmt)
{
    const char *s;
    switch (fmt) {
        case FMT_RANGED:
            nodeset_conf_ranges (ns, true);
            nodeset_conf_separator (ns, ',');
            break;
        case FMT_COMMA:
            nodeset_conf_ranges (ns, false);
            nodeset_conf_separator (ns, ',');
            break;
        case FMT_NEWLINE:
            nodeset_conf_ranges (ns, false);
            nodeset_conf_separator (ns, '\n');
            break;
    }
    s = nodeset_str (ns);
    if (label) {
        if (fmt == FMT_NEWLINE)
            printf ("%-8s\n%s%s", label, s, strlen (s) > 0 ? "\n" : "");
        else
            printf ("%-8s%s\n", label, s);
    } else {
        if (fmt == FMT_NEWLINE)
            printf ("%s%s", s, strlen (s) > 0 ? "\n" : "");
        else
            printf ("%s\n", s);
    }
}

static nodeset_t ns_merge (nodeset_t ns1, nodeset_t ns2)
{
    nodeset_t ns = nodeset_dup (ns1);
    nodeset_itr_t itr;
    uint32_t r;

    if (!ns || !(itr = nodeset_itr_new (ns2)))
        oom ();
    while ((r = nodeset_next (itr)) != NODESET_EOF)
        nodeset_add_rank (ns, r);
    nodeset_itr_destroy (itr);
    return ns;
}

static void ns_print_up (ns_t *ns, fmt_t fmt)
{
    nodeset_t combined = ns_merge (ns->ok, ns->slow);
    nodeset_print (combined, NULL, fmt);
    nodeset_destroy (combined);
}

static void ns_print_down (ns_t *ns, fmt_t fmt)
{
    nodeset_print (ns->fail, NULL, fmt);
}

static void ns_print_all (ns_t *ns, fmt_t fmt)
{
    nodeset_print (ns->ok, "ok:", fmt);
    nodeset_print (ns->slow, "slow:", fmt);
    nodeset_print (ns->fail, "fail:", fmt);
    nodeset_print (ns->unknown, "unknown:", fmt);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

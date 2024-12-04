/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* nodelist.c - compressed encoding of a list of hostnames
 *
 * A nodelist is a pure JSON representation of a list of possibly
 * repeating hostnames. The implementation takes advantage of the
 * tendency to place a numeric suffix on hostanmes of large HPC
 * clusters and uses the rangelist implementation to encode the
 * suffixes of a common hostname prefix.
 *
 * A JSON nodelist is an array of entries (entries are called a
 * "prefix list" in the code below), where each entry represents
 *  one or more hosts. Entries can have the following form:
 *
 * - a single string represents one hostname
 * - an array entry has 2 elements:
 *   1. a common hostname prefix
 *   2. a rangelist representing the set of suffixes
 *      (see rangelist.c).
 *      An empty string (no suffix) is represented as -1.
 *
 * For each prefix list the common prefix is combined with the
 * rangelist-encoded suffixes to form the list of hosts.
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "nodelist.h"
#include "rangelist.h"

struct prefix_list {
    char *prefix;
    struct rangelist *suffixes;
};

struct nodelist {
    zlist_t *list;
};

static void prefix_list_destroy (struct prefix_list *pl)
{
    if (pl) {
        free (pl->prefix);
        rangelist_destroy (pl->suffixes);
        free (pl);
    }
}

static struct prefix_list *prefix_list_new ()
{
    struct prefix_list *pl = calloc (1, sizeof (*pl));
    return pl;
}

static struct prefix_list *
prefix_list_create (const char *prefix, int suffix)
{
    struct prefix_list *pl = prefix_list_new ();
    if (!pl
        || !(pl->suffixes = rangelist_create ())
        || !(pl->prefix = strdup (prefix))) {
        prefix_list_destroy (pl);
        return NULL;
    }
    rangelist_append (pl->suffixes, suffix);
    return pl;
}

void nodelist_destroy (struct nodelist *nl)
{
    if (nl) {
        if (nl->list)
            zlist_destroy (&nl->list);
        free (nl);
    }
}

struct nodelist *nodelist_create ()
{
    struct nodelist *nl = calloc (1, sizeof (*nl));
    if (!nl || !(nl->list = zlist_new ())) {
        nodelist_destroy (nl);
        return NULL;
    }
    return nl;
}

static char *make_hostname (const char *prefix, int64_t n)
{
    char *result = NULL;
    if (n >= 0) {
        if (asprintf (&result, "%s%jd", prefix, (intmax_t)n) < 0)
            return NULL;
    }
    else if (n != RANGELIST_END)
        result = strdup (prefix);
    return result;
}

static char *prefix_list_first (struct prefix_list *pl)
{
    if (pl == NULL)
        return NULL;
    return make_hostname (pl->prefix, rangelist_first (pl->suffixes));
}

static char *prefix_list_next (struct prefix_list *pl)
{
    int64_t n;
    if (pl == NULL)
        return NULL;
    if ((n = rangelist_next (pl->suffixes)) == RANGELIST_END)
        return NULL;
    return make_hostname (pl->prefix, n);
}

char *nodelist_first (struct nodelist *nl)
{
    return prefix_list_first (zlist_first (nl->list));
}

char *nodelist_next (struct nodelist *nl)
{
    char *result = NULL;
    struct prefix_list *pl = zlist_item (nl->list);
    if (pl == NULL)
        return NULL;
    if (!(result = prefix_list_next (pl))) {
        if (!(pl = zlist_next (nl->list)))
            return NULL;
        result = prefix_list_first (pl);
    }
    return result;
}

static int hostname_split (char *name, int *suffix)
{
    int len = strlen (name);
    int n = len - 1;
    while (n >= 0 && isdigit (name[n]))
        n--;
    /*  Now advance past leading zeros (not including a final zero)
     *  These will not be part of the suffix since they can't be represented
     *  as an integer.
     */
    while (name[n+1] == '0' && name[n+2] != '\0')
        n++;
    if (++n == len)
        return 0;
    *suffix = (int) strtol (name+n, NULL, 10);
    name[n] = '\0';
    return 0;
}

static int nodelist_append_prefix_list (struct nodelist *nl,
                                        struct prefix_list *pl)
{
    if (zlist_append (nl->list, pl) < 0)
        return -1;
    zlist_freefn (nl->list, pl, (zlist_free_fn *) prefix_list_destroy, true);
    return 0;
}

int nodelist_append (struct nodelist *nl, const char *host)
{
    int suffix = -1;
    char name [4096];
    struct prefix_list *pl = zlist_tail (nl->list);

    if (strlen (host) > sizeof(name) - 1) {
        errno = E2BIG;
        return -1;
    }
    strcpy (name, host);
    hostname_split (name, &suffix);

    if (pl && streq (name, pl->prefix))
        return rangelist_append (pl->suffixes, suffix);

    if (!(pl = prefix_list_create (name, suffix))
        || nodelist_append_prefix_list (nl, pl) < 0) {
        prefix_list_destroy (pl);
        return -1;
    }
    return 0;
}

int nodelist_append_list_destroy (struct nodelist *nl1, struct nodelist *nl2)
{
    struct prefix_list *pl1 = zlist_tail (nl1->list);
    struct prefix_list *pl2 = zlist_pop (nl2->list);
    if (streq (pl1->prefix, pl2->prefix)) {
        rangelist_append_list (pl1->suffixes, pl2->suffixes);
        prefix_list_destroy (pl2);
        pl2 = zlist_pop (nl2->list);
    }
    while (pl2) {
        nodelist_append_prefix_list (nl1, pl2);
        pl2 = zlist_pop (nl2->list);
    }
    nodelist_destroy (nl2);
    return 0;
}

static json_t *prefix_list_to_json (struct prefix_list *pl)
{
    json_t *o;

    /* special case; singleton host is written as a string to save space */
    if (rangelist_size (pl->suffixes) == 1) {
        char *s = make_hostname (pl->prefix, rangelist_first (pl->suffixes));
        o = json_string (s);
        free (s);
        return o;
    }
    if (!(o = rangelist_to_json (pl->suffixes)))
        return NULL;

    return json_pack ("[so]", pl->prefix, o);
}

json_t *nodelist_to_json (struct nodelist *nl)
{
    json_t *o = json_array ();
    struct prefix_list *pl = zlist_first (nl->list);
    while (pl) {
        json_array_append_new (o, prefix_list_to_json (pl));
        pl = zlist_next (nl->list);
    }
    return o;
}

static struct prefix_list *prefix_list_from_json (json_t *o)
{
    const char *prefix;
    json_t *suffixes;
    struct prefix_list *pl;

    /*  Special case, string is a single host
     */
    if (json_is_string (o)) {
        char name [4096];
        int suffix = -1;
        strncpy (name, json_string_value (o), sizeof (name) - 1);
        hostname_split (name, &suffix);
        return prefix_list_create (name, suffix);
    }
    else if (json_unpack (o, "[so]", &prefix, &suffixes) < 0
        || !(pl = prefix_list_new ()))
        return NULL;
    if (!(pl->prefix = strdup (prefix))
        || !(pl->suffixes = rangelist_from_json (suffixes))) {
        prefix_list_destroy (pl);
        return NULL;
    }
    return pl;
}

struct nodelist *nodelist_from_json (json_t *o)
{
    int i;
    json_t *val;
    struct nodelist *nl = NULL;
    if (!o || !json_is_array (o)) {
        errno = EINVAL;
        return NULL;
    }
    if (!(nl = nodelist_create ()))
        return NULL;

    json_array_foreach (o, i, val) {
        struct prefix_list *pl = prefix_list_from_json (val);
        if (!pl || nodelist_append_prefix_list (nl, pl) < 0) {
            nodelist_destroy (nl);
            return NULL;
        }
    }
    return nl;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

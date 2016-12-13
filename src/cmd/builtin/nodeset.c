/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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

#include "builtin.h"
#include <inttypes.h>
#include <sys/param.h>
#include "src/common/libutil/nodeset.h"

static struct optparse_option nodeset_opts[] = {
    { .name = "cardinality",  .key = 'c',  .has_arg = 0,
      .usage = "Print cardinality (number of members) of single nodeset", },
    { .name = "union",  .key = 'u',  .has_arg = 0,
      .usage = "Print union of all nodesets", },
    { .name = "intersection",  .key = 'i',  .has_arg = 0,
      .usage = "Print intersection of all nodesets", },
    { .name = "subtract",  .key = 's',  .has_arg = 1,
      .usage = "Print intersection of all nodesets", },
    { .name = "expand",  .key = 'e',  .has_arg = 0,
      .usage = "Expand nodesets", },
    { .name = "delimiter",  .key = 'd',  .has_arg = 1,
      .usage = "Set output delimiter", },
    OPTPARSE_TABLE_END,
};

static int find_shortest_nodeset (int nsc, nodeset_t **nsv)
{
    uint32_t count, min = UINT_MAX;
    int i, min_ix = -1;
    for (i = 0; i < nsc; i++) {
        count = nodeset_count (nsv[i]);
        if (count < min) {
            min = count;
            min_ix = i;
        }
    }
    return min_ix;
}

/* Use the shortest nodeset to build the intersection
 * by pruning its values not found in any of the other nodesets.
 * (The returned nodeset is from nsv - didn't bother to copy)
 */
static nodeset_t *nsv_intersection (int nsc, nodeset_t **nsv)
{
    uint32_t rank;
    int i, min = find_shortest_nodeset (nsc, nsv);
    nodeset_iterator_t *itr;
    if (min < 0)
        return NULL;
    itr = nodeset_iterator_create (nsv[min]);
    while ((rank = nodeset_next (itr)) != NODESET_EOF) {
        for (i = 0; i < nsc; i++) {
            if (i == min)
                continue;
            if (!nodeset_test_rank (nsv[i], rank)) {
                nodeset_delete_rank (nsv[min], rank);
                break;
            }
        }
    }
    nodeset_iterator_destroy (itr);
    return nsv[min];
}

/* Use nsv[0] to build intersection.
 * (The returned nodeset is from nsv - didn't bother to copy)
 */
static nodeset_t *nsv_union (int nsc, nodeset_t **nsv)
{
    int i;
    if (nsc < 1)
        return NULL;
    for (i = 1; i < nsc; i++) {
        if (!nodeset_add_string (nsv[0], nodeset_string (nsv[i])))
            return NULL;
    }
    return nsv[0];
}

static void ns_subtract (nodeset_t *ns1, nodeset_t *ns2)
{
    nodeset_iterator_t *itr = nodeset_iterator_create (ns2);
    uint32_t rank;

    while ((rank = nodeset_next (itr)) != NODESET_EOF)
        nodeset_delete_rank (ns1, rank);
    nodeset_iterator_destroy (itr);
}

static int cmd_nodeset (optparse_t *p, int ac, char *av[])
{
    int ix = optparse_option_index (p);
    int nsc = ac - ix;
    nodeset_t *nsp, **nsv = nsc > 0 ? xzmalloc (sizeof (nsv[0]) * nsc) : NULL;
    int i;

    for (i = 0; i < nsc; i++)
        if (!(nsv[i] = nodeset_create_string (av[ix + i])))
            log_errn_exit (EINVAL, "%s", av[ix + i]);

    if (optparse_hasopt (p, "intersection"))
        nsp = nsv_intersection (nsc, nsv);
    else
        nsp = nsv_union (nsc, nsv);

    if (optparse_hasopt (p, "subtract")) {
        const char *s = optparse_get_str (p, "subtract", "");
        nodeset_t *ns = nodeset_create_string (s);
        if (!ns)
            log_errn_exit (EINVAL, "%s", s);
        ns_subtract (nsp, ns);
    }

    if (optparse_hasopt (p, "cardinality")) {
        printf ("%" PRIu32 "\n", nsp ? nodeset_count (nsp) : 0);
    } else if (nsp) {
        const char *delim = optparse_get_str (p, "delimiter", "," );
        if (optparse_hasopt (p, "expand")) {
            nodeset_config_ranges (nsp, false);
            nodeset_config_brackets (nsp, false);
        }
        nodeset_config_separator (nsp, delim[0]);
        if (nodeset_count (nsp) > 0)
            printf ("%s\n", nodeset_string (nsp));
    }

    if (nsv) {
        for (i = 0; i < nsc; i++)
            nodeset_destroy (nsv[i]);
        free (nsv);
    }
    return (0);
}

int subcommand_nodeset_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "nodeset",
        cmd_nodeset,
        "[OPTION] [NODESET]...",
        "Manipulate nodesets",
        0, nodeset_opts);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */

/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <sys/param.h>
#include <stdlib.h>
#include <inttypes.h>

#include "attr.h"
#include "info.h"

#include "src/common/libutil/nodeset.h"

int flux_get_size (flux_t *h, uint32_t *size)
{
    const char *val;

    if (!(val = flux_attr_get (h, "size", NULL)))
        return -1;
    *size = strtoul (val, NULL, 10);
    return 0;
}

int flux_get_rank (flux_t *h, uint32_t *rank)
{
    const char *val;

    if (!(val = flux_attr_get (h, "rank", NULL)))
        return -1;
    *rank = strtoul (val, NULL, 10);
    return 0;
}

/* ns1 = intersection (ns1, ns2)
 */
static int ns_intersection (nodeset_t *ns1, nodeset_t *ns2)
{
    uint32_t rank;
    nodeset_iterator_t *itr;

    if (ns2 && ns1) {
        if (!(itr = nodeset_iterator_create (ns1))) {
            errno = ENOMEM;
            return -1;
        }
        while ((rank = nodeset_next (itr)) != NODESET_EOF) {
            if (!nodeset_test_rank (ns2, rank))
                nodeset_delete_rank (ns1, rank);
        }
        nodeset_iterator_destroy (itr);
    }
    return 0;
}

/* ns1 = ns1 - ns2
 */
static int ns_subtract (nodeset_t *ns1, nodeset_t *ns2)
{
    uint32_t rank;
    nodeset_iterator_t *itr;

    if (ns1 && ns2) {
        if (!(itr = nodeset_iterator_create (ns2))) {
            errno = ENOMEM;
            return -1;
        }
        while ((rank = nodeset_next (itr)) != NODESET_EOF) {
            nodeset_delete_rank (ns1, rank);
        }
        nodeset_iterator_destroy (itr);
    }
    return 0;
}

static nodeset_t *ns_special (flux_t *h, const char *arg)
{
    nodeset_t *ns = NULL;
    uint32_t rank, size;
    int saved_errno;
    char *s;

    if (!arg)
        arg = "";
    if (!strcmp (arg, "self")) {
        if (flux_get_rank (h, &rank) < 0) {
            saved_errno = errno;
            goto error;
        }
        if (!(ns = nodeset_create_rank (rank))) {
            saved_errno = EINVAL;
            goto error;
        }
    } else if (!strcmp (arg, "all")) {
        if ((s = getenv ("FLUX_NODESET_ALL")) != NULL) {
            if (!(ns = nodeset_create_string (s))) {
                saved_errno = EINVAL;
                goto error;
            }
        } else {
            if (flux_get_size (h, &size) < 0) {
                saved_errno = errno;
                goto error;
            }
            if (!(ns = nodeset_create_range (0, size - 1))) {
                saved_errno = EINVAL;
                goto error;
            }
        }
    } else {
        if (!(ns = nodeset_create_string (arg))) {
            saved_errno = EINVAL;
            goto error;
        }
    }
    return ns;
error:
    errno = saved_errno;
    return NULL;
}

const char *flux_get_nodeset (flux_t *h, const char *nodeset,
                              const char *exclude)
{
    char *mask = getenv ("FLUX_NODESET_MASK");
    nodeset_t *ns = NULL, *mns = NULL, *xns = NULL;
    int saved_errno;

    if (!(ns = ns_special (h, nodeset)))
        goto error;
    if (exclude && !(xns = ns_special (h, exclude)))
        goto error;
    if (mask && !(mns = ns_special (h, mask))) {
        errno = EINVAL;
        goto error;
    }
    if (ns_subtract(ns, xns) < 0 || ns_intersection (ns, mns) < 0)
        goto error;
    if (xns)
        nodeset_destroy (xns);
    if (mns)
        nodeset_destroy (mns);
    if (flux_aux_set (h, "flux::nodeset", ns, (flux_free_f) nodeset_destroy) < 0)
        goto error;
    return nodeset_string (ns);
error:
    saved_errno = errno;
    if (ns)
        nodeset_destroy (ns);
    if (xns)
        nodeset_destroy (xns);
    if (mns)
        nodeset_destroy (mns);
    errno = saved_errno;
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

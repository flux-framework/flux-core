/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
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
#include <sys/param.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <flux/core.h>

#include "wreck_jobpath.h"


/*
 *  lwj directory hierarchy parameters:
 *
 *  directory levels is the number of parent directories
 *   (e.g. 3 would result in lwj-active.x.y.z.jobid,
 *    0 is lwj.jobid)
 *
 *  bits_per_directory is the number of prefix bits to use
 *   for each parent directory, results in 2^bits entries
 *   per subdirectory, except for the top-level which will
 *   grow without bound (well up to 64bit lwj id values).
 *
 *  These values can be set as broker attrs during flux-start,
 *   e.g. flux start -o,-Swreck.lwj-dir-levels=3
 *                   -o,-Swreck.lwj-bits-per-dir=8
 */

struct jobpath_ctx {
    int dir_levels;
    int bits_per_dir;
};

static const int default_kvs_dir_levels = 2;
static const int default_kvs_bits_per_dir = 7;

static int attr_set_int (flux_t *h, const char *attr, int val)
{
    char buf [16];
    int n = snprintf (buf, sizeof (buf), "%d", val);
    if (n < 0 || n >= sizeof (buf))
        return (-1);
    return flux_attr_set (h, attr, buf);
}

static int attr_get_int (flux_t *h, const char *attr, int *valp)
{
    long n;
    const char *tmp;
    char *p;

    if ((tmp = flux_attr_get (h, attr, 0)) == NULL)
        return (-1);
    n = strtoul (tmp, &p, 10);
    if (n == LONG_MAX)
        return (-1);
    if ((p == tmp) || (*p != '\0')) {
        errno = EINVAL;
        return (-1);
    }
    *valp = (int) n;
    return (0);
}

static struct jobpath_ctx *getctx (flux_t *h)
{
    const char *auxkey = "flux::wreck_jobpath";
    struct jobpath_ctx *ctx = flux_aux_get (h, auxkey);

    if (!ctx) {
        if (!(ctx = calloc (1, sizeof (*ctx))))
            return NULL;
        ctx->dir_levels = default_kvs_dir_levels;
        ctx->bits_per_dir = default_kvs_bits_per_dir;
        if ((attr_get_int (h, "wreck.lwj-dir-levels", &ctx->dir_levels) < 0)
         && (attr_set_int (h, "wreck.lwj-dir-levels", ctx->dir_levels) < 0))
            goto error;
        if ((attr_get_int (h, "wreck.lwj-bits-per-dir",&ctx->bits_per_dir) < 0)
         && (attr_set_int (h, "wreck.lwj-bits-per-dir",ctx->bits_per_dir) < 0))
            goto error;
        flux_aux_set (h, auxkey, ctx, (flux_free_f)free);
    }
    return ctx;
error:
    free (ctx);
    return NULL;
}

/*
 *  Return as 64bit integer the portion of integer `n`
 *   masked from bit position `a` to position `b`,
 *   then subsequently shifted by `a` bits (to keep
 *   numbers small).
 */
static inline uint64_t prefix64 (uint64_t n, int a, int b)
{
    uint64_t mask = ((-1ULL >> (64 - b)) & ~((1ULL << a) - 1));
    return ((n & mask) >> a);
}

/*
 *  Convert lwj id to kvs path under `lwj-active` using a kind of
 *   prefix hiearchy of max levels `levels`, using `bits_per_dir` bits
 *   for each directory. Returns a kvs key path or NULL on failure.
 */
static char * lwj_to_path (char *buf, int bufsz,
                           uint64_t id, int levels, int bits_per_dir)
{
    int len;
    int nleft;
    int i, n;

    if (bufsz < 4)
        return NULL;
    strcpy (buf, "lwj");
    len = 3;
    nleft = bufsz - len;

    /* Build up kvs directory from lwj. down */
    for (i = levels; i > 0; i--) {
        int b = bits_per_dir * i;
        uint64_t d = prefix64 (id, b, b + bits_per_dir);
        if ((n = snprintf (buf+len, nleft, ".%"PRIu64, d)) < 0 || n > nleft)
            return NULL;
        len += n;
        nleft -= n;
    }
    n = snprintf (buf+len, bufsz - len, ".%"PRIu64, id);
    if (n < 0 || n > nleft)
        return NULL;
    return buf;
}

char *wreck_id_to_path (flux_t *h, char *buf, int bufsz, uint64_t id)
{
    struct jobpath_ctx *ctx;
    if (!(ctx = getctx (h)))
        return NULL;
    return (lwj_to_path (buf, bufsz, id, ctx->dir_levels, ctx->bits_per_dir));
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

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

/* nodeset.c - set of unsigned integer ranks */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include "veb.h"
#include "oom.h"
#include "monotime.h"

#include "nodeset.h"

static const int string_initsize = 4096;
static const uint32_t veb_minsize = 1<<10;

#undef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#undef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))

#define NS_MAGIC 0xfeef0001
struct nodeset_struct {
    int magic;
    int refcount;

    Veb T;      /* Van Emde Boas tree - T.D is data, T.M is size */
                /*  all ops are O(log m), for key bitsize m: 2^m = T.M */
    char conf_separator[2];
    bool conf_ranges;
    bool conf_brackets;
    unsigned int conf_padding;

    char *s;    /* cached string representation */
    int s_size;
    bool s_valid;
};

#define NS_ITR_MAGIC 0xf004feef
struct nodeset_iterator_struct {
    int magic;
    nodeset_t *n;
    uint32_t r;
    bool started;
};

static bool nodeset_single (nodeset_t *n);

#define NS_FIRST(n)    vebsucc ((n)->T,0)
#define NS_NEXT(n,r)   vebsucc ((n)->T,(r)+1)
#define NS_LAST(n)     vebpred ((n)->T,(n)->T.M-1)
#define NS_PREV(n,r)   vebpred ((n)->T,(r)-1)
#define NS_SIZE(n)     ((n)->T.M)
#define NS_TEST(n,r)   (vebsucc ((n)->T,(r))==(r))

#define ABS_MAX_SIZE   (~(uint32_t)0)
#define ABS_MAX_RANK   (~(uint32_t)0 - 1)

static Veb vebdup (Veb T)
{
    uint32_t size = vebsize (T.M);
    Veb t;

    t.k = T.k;
    t.M = T.M;
    if ((t.D = malloc (size)))
        memcpy (t.D, T.D, size);
    return t;
}

nodeset_t *nodeset_create_size (uint32_t size)
{
    nodeset_t *n = malloc (sizeof (*n));
    if (!n)
        oom ();
    n->magic = NS_MAGIC;
    n->refcount = 1;
    n->T = vebnew (size, 0);
    if (!n->T.D)
        oom ();
    n->conf_separator[0] = ',';
    n->conf_separator[1] = '\0';
    n->conf_ranges = true;
    n->conf_brackets = true;
    n->conf_padding = 0;
    n->s = NULL;
    n->s_size = 0;
    n->s_valid = false;
    return n;
}

nodeset_t *nodeset_create (void)
{
    return nodeset_create_size (veb_minsize);
}

nodeset_t *nodeset_dup (nodeset_t *n)
{
    nodeset_t *cpy = malloc (sizeof (*cpy));

    if (!cpy)
        oom ();
    memcpy (cpy, n, sizeof (*cpy));
    cpy->refcount = 1;
    cpy->T = vebdup (n->T);
    if (!cpy->T.D)
        oom ();
    if (cpy->s_size > 0) {
        if (!(cpy->s = malloc (cpy->s_size)))
            oom ();
        memcpy (cpy->s, n->s, cpy->s_size);
    }
    return cpy;
}

static void nodeset_incref (nodeset_t *n)
{
    n->refcount++;
}

static void nodeset_decref (nodeset_t *n)
{
    if (--n->refcount == 0) {
        n->magic = ~NS_MAGIC;
        free (n->T.D);
        if (n->s)
            free (n->s);
        free (n);
    }
}

void nodeset_destroy (nodeset_t *n)
{
    assert (n->magic == NS_MAGIC);
    nodeset_decref (n);
}

nodeset_t *nodeset_create_string (const char *s)
{
    nodeset_t *n = nodeset_create ();
    if (!nodeset_add_string (n, s)) {
        nodeset_destroy (n);
        return NULL;
    }
    return n;
}

nodeset_t *nodeset_create_range (uint32_t a, uint32_t b)
{
    nodeset_t *n = nodeset_create ();
    nodeset_add_range (n, a, b);
    return n;
}

nodeset_t *nodeset_create_rank (uint32_t r)
{
    nodeset_t *n = nodeset_create ();
    nodeset_add_rank (n, r);
    return n;
}

void nodeset_config_separator (nodeset_t *n, char c)
{
    assert (n->magic == NS_MAGIC);
    if (n->conf_separator[0] != c)
        n->s_valid = false;
    n->conf_separator[0] = c;
}

void nodeset_config_ranges (nodeset_t *n, bool enable)
{
    assert (n->magic == NS_MAGIC);
    if (n->conf_ranges != enable)
        n->s_valid = false;
    n->conf_ranges = enable;
}

void nodeset_config_brackets (nodeset_t *n, bool enable)
{
    assert (n->magic == NS_MAGIC);
    if (n->conf_brackets != enable)
        n->s_valid = false;
    n->conf_brackets = enable;
}

void nodeset_config_padding (nodeset_t *n, unsigned int padding)
{
    assert (n->magic == NS_MAGIC);
    if (padding > 10)
        padding = 10;
    if (n->conf_padding != padding)
        n->s_valid = false;
    n->conf_padding = padding;
}

bool nodeset_resize (nodeset_t *n, uint32_t size)
{
    assert (n->magic == NS_MAGIC);

    uint32_t r;
    Veb T;

    if (size < veb_minsize)         /* don't allow size below minimum */
        size = veb_minsize;
    if (size < NS_SIZE (n)) {       /* If shrinking, bump size up to */
        r = NS_FIRST (n);           /*   fit highest rank in set. */
        while (r < NS_SIZE (n)) {
            if (r >= size)
                size = r + 1;
            r = NS_NEXT (n, r);
        }
    }
    if (size != NS_SIZE (n)) {
        T = vebnew (size, 0);
        if (!T.D)
            oom ();
        r = NS_FIRST (n);
        while (r < NS_SIZE (n)) {
            vebput (T, r);
            r = NS_NEXT (n, r);
        }
        free (n->T.D);
        n->T = T;
    }
    return true;
}

#define CHECK_SIZE(o,n) ((n)>(o)&&(o)<=ABS_MAX_SIZE)

static bool nodeset_expandtofit (nodeset_t *n, uint32_t r)
{
    uint32_t size = NS_SIZE (n);
    while (size <= r && CHECK_SIZE(size, size << 1))
        size = size << 1;
    while (size <= r && CHECK_SIZE(size, size + veb_minsize))
        size += veb_minsize;
    if (size <= r && CHECK_SIZE(size, r + 1))
        size = r + 1;
    if (size <= r)
        return false;
    return nodeset_resize (n, size);
}

void nodeset_minimize (nodeset_t *n)
{
    assert (n->magic == NS_MAGIC);
    nodeset_resize (n, 0);
    if (n->s) {
        free (n->s);
        n->s = NULL;
        n->s_size = 0;
        n->s_valid = false;
    }
}

nodeset_iterator_t *nodeset_iterator_create (nodeset_t *n)
{
    nodeset_iterator_t *itr = malloc (sizeof (*itr));
    if (!itr)
        oom ();
    itr->magic = NS_ITR_MAGIC;
    itr->n = n;
    nodeset_incref (itr->n);
    itr->r = NODESET_EOF;
    itr->started = false;
    return itr;
}

void nodeset_iterator_destroy (nodeset_iterator_t *itr)
{
    assert (itr->magic == NS_ITR_MAGIC);
    nodeset_decref (itr->n);
    itr->magic = ~NS_ITR_MAGIC;
    free (itr);
}

uint32_t nodeset_next (nodeset_iterator_t *itr)
{
    if (itr->started)
        itr->r = NS_NEXT (itr->n, itr->r);
    else {
        itr->r = NS_FIRST (itr->n);
        itr->started = true;
    }
    return (itr->r == NS_SIZE (itr->n) ? NODESET_EOF : itr->r);
}

uint32_t nodeset_next_rank (nodeset_t *n, uint32_t r)
{
    uint32_t next = NS_NEXT (n, r);
    return (next == NS_SIZE (n) ? NODESET_EOF : next);
}

void nodeset_iterator_rewind (nodeset_iterator_t *itr)
{
    itr->started = false;
}

bool nodeset_add_rank (nodeset_t *n, uint32_t r)
{
    assert (n->magic == NS_MAGIC);

    if (NS_SIZE (n) <= r && !nodeset_expandtofit (n, r))
        return false;
    vebput (n->T, r);
    n->s_valid = false;
    return true;
}

bool nodeset_add_range (nodeset_t *n, uint32_t a, uint32_t b)
{
    assert (n->magic == NS_MAGIC);

    uint32_t r;
    uint32_t lo = MIN(a,b);
    uint32_t hi = MAX(a,b);

    if (NS_SIZE (n) <= hi && !nodeset_expandtofit (n, hi))
        return false;
    for (r = lo; r <= hi; r++)
        vebput (n->T, r);
    n->s_valid = false;
    return true;
}

void nodeset_delete_rank (nodeset_t *n, uint32_t r)
{
    assert (n->magic == NS_MAGIC);
    if (r <= ABS_MAX_RANK)
        vebdel (n->T, r);
    n->s_valid = false;
}

void nodeset_delete_range (nodeset_t *n, uint32_t a, uint32_t b)
{
    assert (n->magic == NS_MAGIC);

    uint32_t r;
    uint32_t lo = MIN(a,b);
    uint32_t hi = MAX(a,b);

    for (r = lo; r <= hi; r++)
        if (r <= ABS_MAX_RANK)
            vebdel (n->T, r);
    n->s_valid = false;
}

bool nodeset_test_rank (nodeset_t *n, uint32_t r)
{
    assert (n->magic == NS_MAGIC);
    return r < NS_SIZE (n) ? NS_TEST (n, r) : false;
}

bool nodeset_test_range (nodeset_t *n, uint32_t a, uint32_t b)
{
    assert (n->magic == NS_MAGIC);

    uint32_t r;
    uint32_t lo = MIN(a,b);
    uint32_t hi = MAX(a,b);

    if (hi >= NS_SIZE (n) || lo >= NS_SIZE (n))
        return false;
    for (r = lo; r <= hi; r++)
        if (!NS_TEST (n, r))
            return false;
    return true;
}

static void
catstr (char **sp, int *lenp, int *usedp, const char *adds)
{
    int l = strlen (adds);
    int buflen = *lenp;

    while (*usedp + l + 1 > buflen)
        buflen *= 2;
    if (buflen > *lenp) {
        *lenp = buflen;
        if (!(*sp = realloc (*sp, *lenp)))
            oom ();
    }
    strcpy (*sp + *usedp, adds);
    *usedp += l;
}

const char *nodeset_string (nodeset_t *n)
{
    assert (n->magic == NS_MAGIC);

    const char *sep = "";
    int used = 0;
    uint32_t r, lo = 0, hi = 0;
    bool inrange = false;
    char tmp[128];

    if (!n->s_valid) {
        if (!n->s) {
            n->s_size = string_initsize;
            if (!(n->s = malloc (n->s_size)))
                oom ();
        }
        n->s[0] = '\0';
        if (n->conf_brackets && !nodeset_single (n))
            catstr (&n->s, &n->s_size, &used, "[");
        r = NS_FIRST (n);
        while (r < NS_SIZE (n) || inrange) {
            if (n->conf_ranges) {
                if (!inrange) {
                    lo = hi = r;
                    inrange = true;
                } else if (r < NS_SIZE (n) && r == hi + 1) {
                    hi++;
                } else if (lo == hi) {
                    snprintf (tmp, sizeof (tmp), "%s%0*"PRIu32, sep,
                              n->conf_padding, lo);
                    catstr (&n->s, &n->s_size, &used, tmp);
                    sep = n->conf_separator;
                    inrange = false;
                    continue;
                } else {
                    snprintf (tmp, sizeof (tmp), "%s%0*"PRIu32"-%0*"PRIu32, sep,
                             n->conf_padding, lo,
                             n->conf_padding, hi);
                    catstr (&n->s, &n->s_size, &used, tmp);
                    sep = n->conf_separator;
                    inrange = false;
                    continue;
                }
            } else {
                snprintf (tmp, sizeof (tmp), "%s%0*"PRIu32, sep,
                          n->conf_padding, r);
                catstr (&n->s, &n->s_size, &used, tmp);
                sep = n->conf_separator;
                inrange = false;
            }
            if (r < NS_SIZE (n))
                r = NS_NEXT (n, r);
        }
        if (n->s[0] == '[')
            catstr (&n->s, &n->s_size, &used, "]");
        n->s_valid = true;
    }
    return n->s;
}

static bool str2rank (const char *s, uint32_t *rp)
{
    char *endptr = NULL;
    uint32_t r = strtoul (s, &endptr, 10);

    if (endptr == s || *endptr != '\0')
        return false;
    *rp = r;
    return true;
}

typedef enum { OP_ADD, OP_DEL, OP_TEST } op_t;

static bool nodeset_op_string (nodeset_t *n, op_t op, const char *str)
{
    char *cpy;
    int len;
    char *p, *s, *saveptr = NULL, *a1;
    uint32_t lo, hi;
    int count = 0;

    len = strlen (str);
    if (str[0] == '[' && str[len - 1] == ']') { /* hostlist compat */
        if (!(cpy = strdup (str + 1)))
            oom ();
        cpy[len - 2] = '\0';
    } else
        if (!(cpy = strdup (str)))
            oom ();

    a1 = cpy;
    while ((s = strtok_r (a1, ",", &saveptr))) {
        if ((p = strchr (s, '-'))) {
            *p = '\0';
            if (!str2rank (s, &lo) || !str2rank (p + 1, &hi))
                break;
            if (op == OP_DEL) {
                nodeset_delete_range (n, hi, lo);
            } else if (op == OP_ADD) {
                if (!nodeset_add_range (n, hi, lo))
                    break;
            } else if (op == OP_TEST) {
                if (!nodeset_test_range (n, hi, lo))
                    break;
            }
        } else {
            if (!str2rank (s, &lo))
                break;
            if (op == OP_DEL) {
                nodeset_delete_rank (n, lo);
            } else if (op == OP_ADD) {
                if (!nodeset_add_rank (n, lo))
                    break;
            } else if (op == OP_TEST) {
                if (!nodeset_test_rank (n, lo))
                    break;
            }
        }
        a1 = NULL;
        count++;
    }
    free (cpy);
    if (s || (count == 0 && strlen (str) > 0))
        return false;
    return true;
}

bool nodeset_add_string (nodeset_t *n, const char *str)
{
    assert (n->magic == NS_MAGIC);
    return nodeset_op_string (n, OP_ADD, str);
}

bool nodeset_delete_string (nodeset_t *n, const char *str)
{
    assert (n->magic == NS_MAGIC);
    return nodeset_op_string (n, OP_DEL, str);
}

bool nodeset_test_string (nodeset_t *n, const char *str)
{
    assert (n->magic == NS_MAGIC);
    return nodeset_op_string (n, OP_TEST, str);
}

uint32_t nodeset_count (nodeset_t *n)
{
    assert (n->magic == NS_MAGIC);

    uint32_t count = 0;
    uint32_t r;

    r = NS_FIRST (n);
    while (r < NS_SIZE (n)) {
        count++;
        r = NS_NEXT (n, r);
    }
    return count;
}

uint32_t nodeset_min (nodeset_t *n)
{
    assert (n->magic == NS_MAGIC);
    uint32_t r = NS_FIRST (n);
    return r == NS_SIZE (n) ? NODESET_EOF : r;
}

uint32_t nodeset_max (nodeset_t *n)
{
    assert (n->magic == NS_MAGIC);
    uint32_t r = NS_LAST (n);
    return r == NS_SIZE (n) ? NODESET_EOF : r;
}

static bool nodeset_single (nodeset_t *n)
{
    uint32_t len = 0;
    uint32_t r;

    r = NS_FIRST (n);
    while (r < NS_SIZE (n) && len < 2) {
        len++;
        r = NS_NEXT (n, r);
    }
    return (len < 2);
}

uint32_t nodeset_getattr (nodeset_t *n, int attr)
{
    assert (n->magic == NS_MAGIC);
    switch (attr) {
        case NODESET_ATTR_BYTES:
            return vebsize (n->T.M) + n->s_size + sizeof (*n);
        case NODESET_ATTR_SIZE:
            return NS_SIZE (n);
        case NODESET_ATTR_MINSIZE:
            return veb_minsize;
        case NODESET_ATTR_MAXSIZE:
            return ABS_MAX_SIZE;
        case NODESET_ATTR_MAXRANK:
            return ABS_MAX_RANK;
        default:
            return 0;
    }
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

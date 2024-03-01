/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* rangelist.c - compressed encoding of a list of integers
 *
 * The rangelist encoding uses a combination of run-length, range
 * and delta encoding to compress a possibly large set of integers
 * into a somewhat compact JSON array.
 *
 * This implementation is meant for encoding data for the MPIR
 * process table in a space efficient manner, and takes advantage
 * of the fact that multiple PIDs and hostnames with numeric
 * suffixes are usually adjacent (or repeated in the case of hostnames)
 *
 * A rangelist is an array of entries that follow these rules:
 *
 * - If an entry is a single integer then it represents a single number
 *   which is delta encoded from the previous entry (or 0 if this is the
 *   first entry in the rangelist).
 *
 * - If an entry is an array, it will have two or three elements. The first
 *   element is delta encoded from the previous entry (or 0 if no previous
 *   entry) and represents the start value for a set of integers.
 *
 *   - if the second element is > 0, then it represents a run-length
 *     encoded set of integers beginning at start. E.g. [1234,4] represents
 *     1234, 1235, 1236, 1237.
 *
 *   - if the second element is < 0, then it represents a number of
 *     repeats of the same value, e.g. [18, -3] represents [18, 18, 18].
 *
 *   - if there is a third element, this indicates that the first two
 *     elements are repeated N times, e.g. [1, -1], [1 -1], [1, -1] is
 *     equivalent to [1, -1, 2].
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "rangelist.h"

struct range {
    int64_t min;
    int64_t size;
    bool is_rle;
};

struct rangelist {
    bool nodelta;

    int64_t total;
    zlist_t *ranges;

    struct range *current;
    int64_t offset;
};

static void range_destroy (struct range *r)
{
    free (r);
}

static struct range *range_create (int64_t min, int64_t max)
{
    struct range *r = calloc (1, sizeof (*r));
    if (!r)
        return NULL;
    r->min = min;
    r->size = max - min + 1;
    return r;
}

static inline int64_t range_count (struct range *r)
{
    return r->size;
}

static inline int64_t range_max (struct range *r)
{
    if (r->is_rle)
        return r->min;
    return r->min + r->size - 1;
}

static struct range *range_copy (const struct range *r)
{
    struct range *new = calloc (1, sizeof (*r));
    if (!r)
        return NULL;
    *new = *r;
    return new;
}

void rangelist_destroy (struct rangelist *rl)
{
    if (rl) {
        if (rl->ranges)
            zlist_destroy (&rl->ranges);
        free (rl);
    }
}

struct rangelist *rangelist_create (void)
{
    struct rangelist *rl = calloc (1, sizeof (*rl));
    if (!rl || !(rl->ranges = zlist_new ())) {
        rangelist_destroy (rl);
        return NULL;
    }
    return rl;
}

int64_t rangelist_size (struct rangelist *rl)
{
    if (rl)
        return rl->total;
    return -1;
}

int64_t rangelist_first (struct rangelist *rl)
{
    rl->current = zlist_first (rl->ranges);
    rl->offset = 0;
    return  rl->current->min;
}

static int64_t rangelist_advance (struct rangelist *rl)
{
    rl->offset = 0;
    if (!(rl->current = zlist_next (rl->ranges)))
        return RANGELIST_END;
    return rl->current->min;
}

int64_t rangelist_next (struct rangelist *rl)
{
    int64_t next;

    if (!rl || !rl->current)
        return RANGELIST_END;

    if (++rl->offset >= rl->current->size)
        return rangelist_advance (rl);

    if (rl->current->is_rle)
        next = rl->current->min;
    else
        next = rl->current->min + rl->offset;

    return next;
}

static int rangelist_append_range (struct rangelist *rl, struct range *r)
{
    if (zlist_append (rl->ranges, r) < 0)
        return -1;
    zlist_freefn (rl->ranges, r, (zlist_free_fn *) range_destroy, true);
    rl->total += range_count (r);
    return 0;
}

static int rangelist_append_new_range (struct rangelist *rl,
                                       int64_t min,
                                       int64_t max)
{
    int rc;
    struct range *r = range_create (min, max);
    if (r == NULL)
        return -1;
    rc = rangelist_append_range (rl, r);
    return rc;
}

int rangelist_append (struct rangelist *rl, int64_t n)
{
    struct range *r = zlist_tail (rl->ranges);
    if (r) {
        if (!r->is_rle && n == range_max (r) + 1) {
            r->size++;
            rl->total++;
            return 0;
        }
        else if (n == r->min && (r->is_rle || r->size == 1)) {
            r->is_rle = true;
            r->size++;
            rl->total++;
            return 0;
        }
    }
    return rangelist_append_new_range (rl, n, n);
}

static bool try_range_combine (struct range *prev, struct range *next)
{
    if (next->min == prev->min && (prev->is_rle || prev->size == 1)) {
        prev->is_rle = true;
        prev->size += range_count (next);
        return true;
    }
    if (next->min == range_max (prev) + 1 && !prev->is_rle) {
        prev->size += range_count (next);
        return true;
    }
    return false;
}

int rangelist_append_list (struct rangelist *rl, struct rangelist *new)
{
    struct range *prev = zlist_tail (rl->ranges);
    struct range *next = zlist_first (new->ranges);

    /* Combine first range with last range if possible */
    if (try_range_combine (prev, next)) {
        rl->total += range_count (next);
        next = zlist_next (new->ranges);
    }

    while (next) {
        struct range *r = range_copy (next);
        if (rangelist_append_range (rl, r) < 0)
            return -1;
        next = zlist_next (new->ranges);
    }
    return 0;
}

json_t *range_to_json (struct range *r, int64_t base)
{
    json_t *o = NULL;
    int64_t range = range_count (r) - 1;
    json_t *val = json_integer (r->min - base);
    if (range == 0)
        o = val;
    else if (!val
            || !(o = json_array ())
            || json_array_append_new (o, val) < 0
            || !(val = json_integer (r->is_rle ? -range : range))
            || json_array_append_new (o, val) < 0) {
        json_decref (o);
        json_decref (val);
        return NULL;
    }
    return o;
}

static json_int_t range_json_val (json_t *array, size_t index)
{
    return json_integer_value (json_array_get (array, index));
}

static bool range_json_equal (json_t *a, json_t *b)
{
    if (range_json_val (a, 0) == range_json_val (b, 0)
        && range_json_val (a, 1) == range_json_val (b, 1))
        return true;
    return false;
}

static int increment_range_repeat (json_t *range)
{
    json_t *val = NULL;
    if (json_array_size (range) == 3) {
        json_int_t repeat = range_json_val (range, 2);
        if (!(val = json_integer (repeat + 1))
            || json_array_set_new (range, 2, val) < 0)
            goto error;
        return 0;
    }
    if (!(val = json_integer (1))
        || json_array_append_new (range, val) < 0)
        goto error;
    return 0;
error:
    json_decref (val);
    return -1;
}

static bool check_previous_repeat (json_t *array, json_t *range)
{
    json_t *prev;
    size_t size = json_array_size (array);
    bool result = false;

    if ((size = json_array_size (array)) == 0
        || !(prev = json_array_get (array, size - 1))
        || json_array_size (prev) < 2)
        return false;
    if ((result = range_json_equal (prev, range))
        && increment_range_repeat (prev) < 0)
        return false;
    return result;
}

json_t *rangelist_to_json (struct rangelist *rl)
{
    struct range *r;
    int64_t base = 0;
    json_t *result = json_array ();
    if (!result)
        return NULL;
    r = zlist_first (rl->ranges);
    while (r) {
        json_t *range = range_to_json (r, base);
        if (!range)
            goto error;
        if (check_previous_repeat (result, range))
            json_decref (range);
        else if (json_array_append_new (result, range) < 0) {
            json_decref (range);
            goto error;
        }
        base = range_max (r);
        r = zlist_next (rl->ranges);
    }
    return result;
error:
    json_decref (result);
    return NULL;
}

static struct range *range_from_json (json_t *o, int64_t base, int *repeat)
{
    int64_t min;
    int64_t range = 0;

    if (repeat)
        *repeat = 0;

    if (json_is_array (o)) {
        size_t size = json_array_size (o);
        if (size < 2 || size > 3) {
            errno = EINVAL;
            return NULL;
        }
        min = json_integer_value (json_array_get (o, 0));
        range = json_integer_value (json_array_get (o, 1));
        if (size == 3 && repeat != NULL)
            *repeat = json_integer_value (json_array_get (o, 2));
    }
    else {
        if (!json_is_integer (o)) {
            errno = EINVAL;
            return NULL;
        }
        min = json_integer_value (o);
    }
    min = min + base;

    /* range < 0 is an encoded run of "range" members */
    if (range < 0) {
        struct range *r = range_create (min, min);
        r->is_rle = true;
        r->size = -range + 1;
        return (r);
    }
    return range_create (min, min + range);
}

struct rangelist *rangelist_from_json (json_t *o)
{
    int i;
    int base = 0;
    struct rangelist *rl;
    json_t *val;

    if (!o || !json_is_array (o)) {
        errno = EINVAL;
        return NULL;
    }
    if (!(rl = rangelist_create ()))
        return NULL;
    json_array_foreach (o, i, val) {
        int repeat;
        struct range *r = range_from_json (val, base, &repeat);

        if (!r || rangelist_append_range (rl, r) < 0) {
            range_destroy (r);
            goto err;
        }
        base = range_max (r);

        /* Handle repeating ranges */
        for (int j = 0; j < repeat; j++) {
            if (!(r = range_from_json (val, base, NULL))
                || rangelist_append_range (rl, r) < 0) {
                range_destroy (r);
                goto err;
            }
            base = range_max (r);
        }
    }
    return rl;
err:
    rangelist_destroy (rl);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <jansson.h>
#include <flux/idset.h>

#include "resourcecount.h"

struct resourcecount {
    unsigned int min; // also stores simple integer value
    unsigned int max;
    unsigned int operand;
    char operator;
    bool isinteger;
    bool isrange;
    bool isunbounded;
    struct idset *idset;
};

void set_error (json_error_t *error, const char *fmt, ...)
{
    va_list ap;

    if (error) {
        va_start (ap, fmt);
        vsnprintf (error->text, sizeof (error->text), fmt, ap);
        va_end (ap);
    }
}

struct resourcecount *resourcecount_create (json_t *json_count,
                                            json_error_t *error)
{
    struct resourcecount *count;
    const char *operator = NULL;
    json_int_t integer_value;
    json_int_t min;
    json_int_t max = RESOURCECOUNT_MAX;
    json_int_t operand = 1;

    if (json_is_string (json_count)) {
        if (!(count = resourcecount_decode (json_string_value (json_count)))) {
            set_error (error,
                       "resourcecount_create: Failed to decode '%s' as idset or range",
                       json_string_value (json_count));
            goto error;
        }
        return count;
    }
    if (!(count = calloc (1, sizeof (*count)))) {
        set_error (error, "resourcecount_create: Out of memory");
        goto error;
    }
    count->idset = NULL;
    if (json_is_integer (json_count)) {
        // have to check negativity and upper bound before assigning to unsigned int
        integer_value = json_integer_value (json_count);
        if (integer_value < 0 || integer_value > RESOURCECOUNT_MAX) {
            set_error (error,
                       "resourcecount_create: integer count must be in [0, %u]",
                       RESOURCECOUNT_MAX);
            goto error;
        }
        count->min = integer_value;
        count->isinteger = true;
        return count;
    }
    if (json_object_size (json_count) == 0) {
        set_error (error, "resourcecount_create: Malformed jobspec resource count");
        goto error;
    }
    count->isunbounded = json_object_get (json_count, "max") == NULL;
    if (json_unpack_ex (json_count, error, 0,
                        "{s:I, s?I, s?I, s?s}",
                        "min", &min,
                        "max", &max,
                        "operand", &operand,
                        "operator", &operator) < 0) {
        goto error;
    }
    // have to check positivity and upper bounds before assigning to unsigned ints
    if (min < 1 || min > RESOURCECOUNT_MAX
        || max < 1 || max > RESOURCECOUNT_MAX
        || operand < 1 || operand > RESOURCECOUNT_MAX) {
        set_error (error,
                   "resourcecount_create: min, max, and operand must be in [1, %u]",
                   RESOURCECOUNT_MAX);
        goto error;
    }
    count->min = min;
    count->max = max;
    count->operand = operand;
    count->operator = operator ? operator[0] : '+';
    // check validity of operator/operand combination
    switch (count->operator) {
        case '^':
        case '*':
            if (count->operand < 2) {
                set_error (error,
                           "resourcecount_create: operand must be >= 2 "
                           "for '*' or '^' operators");
                goto error;
            }
        case '+':
            break;
        default:
            set_error (error,
                       "resourcecount_create: unknown operator '%c'",
                       count->operator);
            goto error;
    }
    if (count->max < count->min) {
        set_error (error, "resourcecount_create: max must be >= min");
        goto error;
    }
    count->isrange = true;
    if (resourcecount_first (count) == RESOURCECOUNT_INVALID_VALUE) {
        set_error (error, "resourcecount_create: range has no valid values");
        goto error;
    }
    return count;
error:
    resourcecount_destroy (count);
    return NULL;
}

void resourcecount_destroy (struct resourcecount *count)
{
    if (count) {
        int saved_errno = errno;
        idset_destroy (count->idset);
        free (count);
        errno = saved_errno;
    }
}

/* strtoui() with result parameter, assumed base=10.
 * Fail if no digits, leading non-digits, negative, or leading zero.
 */
static int strtoui_check (const char *s, char **endptr, unsigned int *result)
{
    unsigned long long in;

    errno = 0;
    in = strtoull (s, endptr, 10);
    // error, leading zero, non-digit (catches negative sign), or too large input
    if (errno != 0
        || (*s == '0' && *endptr - s > 1)
        || !isdigit (*s)
        || in > RESOURCECOUNT_MAX) {
        errno = errno ? errno : EINVAL;
        return -1;
    }
    *result = in;
    return 0;
}

struct resourcecount *resourcecount_decode (const char *s)
{
    struct resourcecount *count = NULL;
    char *cpy = NULL;
    char *ep;
    int len;
    json_t *json_input;

    if (!s) {
        errno = EINVAL;
        return NULL;
    }
    len = strlen (s);

    // if it starts with a brace, decode as JSON
    if (*s == '{') {
        if (!(json_input = json_loads (s, 0, NULL))) {
            goto error_inval;
        }
        count = resourcecount_create (json_input, NULL);
        json_decref (json_input);
        if (!count) {
            goto error_inval;
        }
        return count;
    }
    if (!(count = calloc (1, sizeof (*count)))
        || !(cpy = strdup (s))) {
        goto error;
    }
    count->idset = NULL;
    ep = cpy;
    if (len >= 2 && s[0] == '[' && s[len - 1] == ']') {
        cpy[len - 1] = '\0';
        ++ep;
    }
    if (strchr (ep, '[') || strchr (ep, ']')) {
        // mismatched or nested brackets
        goto error_inval;
    }
    // first try as a simple integer
    if (strtoui_check (ep, &ep, &count->min) < 0) {
        goto error;
    } else if (*ep == '\0') {
        count->isinteger = true;
        goto success;
    }
    // assume it wasn't just a simple integer and carry on; first try as ideset
    if ((count->idset = idset_decode (s))
         && !idset_empty (count->idset)
         && idset_first (count->idset) != 0) {
        count->min = 0;
        goto success;
    }
    // decoding as an idset failed or was invalid, so try as an RFC14 range
    idset_destroy (count->idset);
    count->idset = NULL;
    if (count->min < 1) {
        goto error_inval;
    }
    count->isrange = true;
    count->operator = '+';
    count->operand = 1;
    if (*ep == '-') {
        if (strtoui_check (ep+1, &ep, &count->max) < 0) {
            goto error;
        } else if (count->max < count->min) {
            goto error_inval;
        }
    } else if (*ep == '+') {
        count->max = RESOURCECOUNT_MAX;
        count->isunbounded = true;
        ++ep;
    } else {
        goto error_inval;
    }
    if (*ep == '\0') {
        goto success;
    } else if (*ep != ':') {
        goto error_inval;
    }
    if (strtoui_check (ep+1, &ep, &count->operand) < 0) {
        goto error;
    } else if (count->operand < 1) {
        goto error_inval;
    }
    if (*ep == '\0') {
        goto success;
    } else if (*ep != ':') {
        goto error_inval;
    }
    switch (ep[1]) {
        case '^':
        case '*':
            if (count->operand < 2) {
                goto error_inval;
            }
        case '+':
            count->operator = ep[1];
            break;
        default:
            goto error_inval;
    }
    if (resourcecount_first (count) == RESOURCECOUNT_INVALID_VALUE) {
        // range has no valid values
        goto error_inval;
    }
    if (ep[2] != '\0') {
        // input continues after operator
        goto error_inval;
    }
success:
    free (cpy);
    return count;
error_inval:
    errno = EINVAL;
error:
    resourcecount_destroy (count);
    free (cpy);
    return NULL;
}

char *resourcecount_encode (const struct resourcecount *count, int flags)
{
    char *str;
    int sz;
    int i = 0;
    int valid_flags = RESOURCECOUNT_FLAG_BRACKETS | RESOURCECOUNT_FLAG_SHORT;

    if (!count || ((flags & valid_flags) != flags)) {
        errno = EINVAL;
        return NULL;
    }
    if (count->idset) {
        return idset_encode (count->idset, flags);
    } else if (count->isinteger) {
        if (asprintf (&str, "%u", count->min) < 0) {
            return NULL;
        }
        return str;
    }
    // otherwise encode as RFC14 range
    sz = snprintf (NULL, 0, "[%u-%u:%u:+]",
                   RESOURCECOUNT_MAX, RESOURCECOUNT_MAX, RESOURCECOUNT_MAX);
    if (!(str = calloc (sz+1, sizeof (*str)))) {
        return NULL;
    }
    if (count->min == count->max) {
        sprintf (str, "%u", count->min);
        return str;
    }
    if (flags & RESOURCECOUNT_FLAG_BRACKETS) {
        i += sprintf (str, "[");
    }
    if (count->isunbounded) {
        i += sprintf (str+i, "%u+", count->min);
    } else {
        i += sprintf (str+i, "%u-%u", count->min, count->max);
    }
    if (!(flags & RESOURCECOUNT_FLAG_SHORT) || (count->operand > 1)) {
        i += sprintf (str+i, ":%u", count->operand);
    }
    if (!(flags & RESOURCECOUNT_FLAG_SHORT) || (count->operator != '+')) {
        i += sprintf (str+i, ":%c", count->operator);
    }
    if (flags & RESOURCECOUNT_FLAG_BRACKETS) {
        sprintf (str+i, "]");
    }
    return str;
}

/* Return 'base' raised to the power 'exp', or RESOURCECOUNT_INVALID_VALUE
 * if the result would exceed RESOURCECOUNT_MAX.
 */
static unsigned int power (unsigned int base, unsigned int exp)
{
    unsigned int result = base;

    if (base == 1) {
        return 1;
    }
    for (unsigned int i = 1; i < exp; ++i) {
        if (result > RESOURCECOUNT_MAX / base) {
            result = RESOURCECOUNT_INVALID_VALUE;
            break;
        }
        result *= base;
    }
    return result;
}

/* Return floor (value ** (1/exp)) for value >= 1, exp >= 2.
 */
static unsigned int root (unsigned int value, unsigned int exp)
{
    unsigned int lo = 1;
    unsigned int hi = value;

    while (lo < hi) {
        unsigned int mid = (lo + hi + 1) / 2;
        if (power (mid, exp) <= value) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

unsigned int resourcecount_first (const struct resourcecount *count)
{
    unsigned int first = RESOURCECOUNT_INVALID_VALUE;

    if (count) {
        if (count->isinteger) {
            first = count->min;
        } else if (count->isrange) {
            if (count->operator == '^') {
                unsigned int base = root (count->min, count->operand);
                first = power (base, count->operand);
                if (first < count->min) {
                    first = power (base + 1, count->operand);
                    if (first > count->max) {
                        first = RESOURCECOUNT_INVALID_VALUE;
                    }
                }
            } else {
                first = count->min;
            }
        } else if (count->idset) {
            first = idset_first (count->idset);
        }
    }
    return first;
}

unsigned int resourcecount_next (const struct resourcecount *count, unsigned int value)
{
    unsigned int next = RESOURCECOUNT_INVALID_VALUE;

    if (count) {
        if (count->isrange) {
            if (count->operator == '+') {
                if (count->max > value && count->max - value >= count->operand) {
                    next = value + count->operand;
                }
            } else if (count->operator == '*') {
                next = count->max / count->operand < value ? RESOURCECOUNT_INVALID_VALUE
                                                           : value * count->operand;
            } else if (count->operator == '^') {
                next = power (root (value, count->operand) + 1, count->operand);
                if (next > count->max) {
                    next = RESOURCECOUNT_INVALID_VALUE;
                }
            }
        } else if (count->idset) {
            next = idset_next (count->idset, value);
        }
    }
    return next;
}

unsigned int resourcecount_bounded_last (const struct resourcecount *count,
                                         unsigned int bound)
{
    unsigned int last = RESOURCECOUNT_INVALID_VALUE;

    if (count) {
        if (count->isinteger && count->min <= bound) {
            last = count->min;
        } else if (count->isrange && bound >= count->min) {
            bound = bound < count->max ? bound : count->max;
            if (count->operator == '+') {
                last = bound - ((bound - count->min) % count->operand);
            } else if (count->operator == '^') {
                last = power (root (bound, count->operand), count->operand);
                if (last < count->min) {
                    last = RESOURCECOUNT_INVALID_VALUE;
                }
            } else {
                unsigned int tmp = count->min;
                while (tmp <= bound) {
                    last = tmp;
                    tmp = resourcecount_next (count, tmp);
                }
            }
        } else if (count->idset) {
            last = bound < idset_universe_size (count->idset)
                       ? idset_prev (count->idset, bound + 1)
                       : idset_last (count->idset);
        }
    }
    return last;
}

unsigned int resourcecount_last (const struct resourcecount *count)
{
    return resourcecount_bounded_last (count, RESOURCECOUNT_MAX);
}

bool resourcecount_is_integer (const struct resourcecount *count)
{
    return (count && count->isinteger);
}

bool resourcecount_is_unbounded (const struct resourcecount *count)
{
    return (count && count->isunbounded);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

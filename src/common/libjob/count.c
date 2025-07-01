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

#include "count.h"

void set_error (json_error_t *error, const char *fmt, ...)
{
    va_list ap;

    if (error) {
        va_start (ap, fmt);
        vsnprintf (error->text, sizeof (error->text), fmt, ap);
        va_end (ap);
    }
}

struct count *count_create (json_t *json_count,
                            json_error_t *error)
{
    struct count *count;
    const char *operator = NULL;
    json_int_t min;
    json_int_t max = COUNT_MAX;
    json_int_t operand = 1;

    if (json_is_string (json_count)) {
        if (!(count = count_decode (json_string_value (json_count)))) {
            set_error (error,
                       "create_count: Failed to decode '%s' as idset or range",
                       json_string_value (json_count));
            goto error;
        }
        return count;
    }
    if (!(count = calloc (1, sizeof (*count)))) {
        set_error (error, "create_count: Out of memory");
        goto error;
    }
    count->idset = NULL;
    if (json_is_integer (json_count)) {
        // have to check positivity first before assigning to unsigned int
        if (json_integer_value (json_count) < 1) {
            set_error (error, "create_count: integer count must be >= 1");
            goto error;
        }
        count->integer = json_integer_value (json_count);
        return count;
    }
    if (json_object_size (json_count) == 0) {
        set_error (error, "create_count: Malformed jobspec resource count");
        goto error;
    }
    if (json_unpack_ex(json_count, error, 0,
                       "{s:I, s?I, s?I, s?s}",
                       "min", &min,
                       "max", &max,
                       "operand", &operand,
                       "operator", &operator) < 0) {
        goto error;
    }
    // have to check positivity first before assigning to unsigned ints
    if (min < 1 || max < 1 || operand < 1) {
        set_error (error, "create_count: min, max, and operand must be >= 1");
        goto error;
    }
    count->min = min;
    count->max = max;
    count->operand = operand;
    count->operator = operator ? operator[0] : '+';
    // check validity of operator/operand combination
    switch (count->operator) {
    case '^':
        if (count->min < 2) {
            set_error (error, "create_count: min must be >= 2 for '^' operator");
            goto error;
        }
    case '*':
        if (count->operand < 2) {
            set_error (error, "create_count: operand must be >= 2 for '*' or '^' operators");
            goto error;
        }
    case '+':
        break;
    default:
        set_error (error, "create_count: unknown operator '%c'", count->operator);
        goto error;
    }
    if (count->max < count->min) {
        set_error (error, "create_count: max must be >= min");
        goto error;
    }
    count->isrange = true;
    return count;
error:
    free (count);
    return NULL;
}

void count_destroy (struct count *count)
{
    if (count) {
        free (count->idset);
        free (count);
    }
}

/* strtoui() with result parameter, assumed base=10.
 * Fail if no digits, leading non-digits, negative, or leading zero.
 */
static int strtoui_check (const char *s, char **endptr, unsigned int *result)
{
    long long in;

    errno = 0;
    in = strtoll (s, endptr, 10);
    // error, leading zero, non-digit, or negative input
    if (errno != 0 || *s == '0' || !isdigit (*s) || in < 1) {
        errno = errno ? errno : EINVAL;
        return -1;
    }
    *result = in;
    return 0;
}

struct count *count_decode (const char *s)
{
    struct count *count = NULL;
    char *cpy = NULL;
    char *ep;
    int len = strlen (s);
    json_t *json_input;

    // if it starts with a brace, decode as JSON
    if (*s == '{') {
        if (!(json_input = json_loads (s, 0, NULL))) {
            goto error_inval;
        }
        count = count_create (json_input, NULL);
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
    if (strtoui_check (ep, &ep, &count->min) < 0) {
        goto error;
    } else if (*ep == '\0') {
        count->integer = count->min;
        count->min = 0;
        goto success;
    }
    // assume it wasn't just a simple integer and carry on
    if ((count->idset = idset_decode (s))
         && !idset_empty (count->idset)
         && idset_first (count->idset) != 0) {
        count->min = 0;
        goto success;
    }
    // decoding as an idset failed or was invalid, so try as an RFC14 range
    free (count->idset);
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
        count->max = COUNT_MAX;
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
    }
    if (*ep == '\0') {
        goto success;
    } else if (*ep != ':') {
        goto error_inval;
    }
    switch (ep[1]) {
        case '*':
        case '^':
            if (count->operand < 2) {
                goto error_inval;
            }
        case '+':
            count->operator = ep[1];
            break;
        default:
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
    free (count);
    free (cpy);
    return NULL;
}

char *count_encode (const struct count *count, int flags)
{
    char *str;
    int sz;
    int i = 0;
    int valid_flags = COUNT_FLAG_BRACKETS | COUNT_FLAG_SHORT;

    if (!count || ((flags & valid_flags) != flags)) {
        errno = EINVAL;
        return NULL;
    }
    if (count->idset) {
        return idset_encode (count->idset, flags);
    } else if (count->integer) {
        if (asprintf (&str, "%u", count->integer) < 0) {
            return NULL;
        }
        return str;
    }
    // otherwise encode as RFC14 range
    sz = snprintf(NULL, 0, "[%u-%u:%u:+]", COUNT_MAX, COUNT_MAX, COUNT_MAX);
    if (!(str = calloc (sz+1, sizeof (*str)))) {
        return NULL;
    }
    if (count->min == count->max) {
        sprintf (str, "%u", count->min);
        return str;
    }
    if (flags & COUNT_FLAG_BRACKETS) {
        i += sprintf (str, "[");
    }
    if (count->max == COUNT_MAX) {
        i += sprintf (str+i, "%u+", count->min);
    } else {
        i += sprintf (str+i, "%u-%u", count->min, count->max);
    }
    if (!(flags & COUNT_FLAG_SHORT) || (count->operand > 1)) {
        i += sprintf (str+i, ":%u", count->operand);
    }
    if (!(flags & COUNT_FLAG_SHORT) || (count->operator != '+')) {
        i += sprintf (str+i, ":%c", count->operator);
    }
    if (flags & COUNT_FLAG_BRACKETS) {
        sprintf (str+i, "]");
    }
    return str;
}

unsigned int count_first (const struct count *count)
{
    unsigned int first = COUNT_INVALID_VALUE;

    if (count) {
        if (count->integer) {
            first = count->integer;
        } else if (count->isrange) {
            first = count->min;
        } else if (count->idset) {
            first = idset_first (count->idset);
        }
    }
    return first;
}

unsigned int count_next (const struct count *count, unsigned int value)
{
    unsigned int next = COUNT_INVALID_VALUE;

    if (count) {
        if (count->isrange) {
            switch (count->operator) {
                case '+':
                    next = value + count->operand;
                    break;
                case '*':
                    next = value * count->operand;
                    break;
                case '^':
                    next = value;
                    for (unsigned int i = 1; i < count->operand; ++i) {
                        next *= value;
                    }
            }
            if (next > count->max) {
                next = COUNT_INVALID_VALUE;
            }
        } else if (count->idset) {
            next = idset_next (count->idset, value);
        }
    }
    return next;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
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
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libidset/idset.h"
#include "ccan/str/str.h"

#include "cronodate.h"

struct cronodate_item {
    struct idset *set;
    char *encoding;
};

struct cronodate {
    struct cronodate_item item [TM_MAX_ITEM];
};

static char * weekdays [] = {
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday"
};
#define WEEKDAY_COUNT 7

static char * months [] = {
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"
};
#define MONTH_COUNT 12


int tm_unit_min (tm_unit_t item)
{
    switch (item) {
    case TM_SEC:
    case TM_MIN:
    case TM_HOUR:
    case TM_WDAY:
    case TM_MON:
    case TM_YEAR: // 1900
        return 0;
    case TM_MDAY:
        return 1;
    default:
        break;
    }
    return -1;
}

int tm_unit_max (tm_unit_t item)
{
    switch (item) {
    case TM_SEC:
        return 60;
    case TM_MIN:
        return 59;
    case TM_HOUR:
        return 23;
    case TM_MDAY:
        return 31;
    case TM_MON:
        return 11;
    case TM_WDAY:
        return 6;
    case TM_YEAR: // 3000
        return 3000-1900;
    default:
        break;
    }
    return -1;
}

const char *tm_unit_string (tm_unit_t item)
{
    switch (item) {
    case TM_SEC:
        return "second";
    case TM_MIN:
        return "minute";
    case TM_HOUR:
        return "hour";
    case TM_MDAY:
        return "mday";
    case TM_MON:
        return "month";
    case TM_WDAY:
        return "weekday";
    case TM_YEAR:
        return "year";
    default:
        break;
    }
    return "unknown";
}

int tm_string_to_weekday (const char *day)
{
    int i;
    if (day == NULL)
        return -1;
    for (i = 0; i < WEEKDAY_COUNT; i++) {
        if (strncasecmp (weekdays [i], day, strlen (day)) == 0)
            return i;
    }
    return -1;
}

const char *tm_weekday_string (int w)
{
    if ((w < 0) || (w > WEEKDAY_COUNT - 1))
        return NULL;
    return weekdays [w];
}

int tm_string_to_month (const char *mon)
{
    int i;
    if (mon == NULL)
        return -1;
    for (i = 0; i < MONTH_COUNT; i++) {
        if (strncasecmp (months [i], mon, strlen (mon)) == 0)
            return i;
    }
    return -1;
}

const char *tm_month_string (int m)
{
    if ((m < 0) || (m > MONTH_COUNT - 1))
        return NULL;
    return months [m];
}


void cronodate_destroy (cronodate_t *d)
{
    int i;
    for (i = 0; i < TM_MAX_ITEM; i++) {
        idset_destroy (d->item [i].set);
        free (d->item [i].encoding);
    }
    free (d);
}

cronodate_t * cronodate_create ()
{
    int i;
    cronodate_t *d;

    if (!(d = calloc (1, sizeof (*d))))
        return NULL;

    for (i = 0; i < TM_MAX_ITEM; i++) {
        struct idset *n = idset_create (tm_unit_max (i) + 1,
                                        IDSET_FLAG_AUTOGROW);
        if (n == NULL) {
            cronodate_destroy (d);
            return NULL;
        }
        d->item [i].set = n;
    }
    return d;
}

static int string2int (const char *s)
{
    char *endptr;
    unsigned long n = strtoul (s, &endptr, 0);
    if (n == ULONG_MAX) /* ERANGE set by strtol */
        return -1;
    if (endptr == s || *endptr != '\0') {
        errno = EINVAL;
        return -1;
    }
    return ((int) n);
}

static int tm_string2int (const char *s, tm_unit_t u)
{
    int n = string2int (s);
    if (n < 0) {
        // Not an integer: Try parsing weekday and month by name
        if (u == TM_WDAY)
            n = tm_string_to_weekday (s);
        else if (u == TM_MON)
            n = tm_string_to_month (s);
    }
    return n;
}

static int get_range (const char *r, tm_unit_t u, int *lo, int *hi)
{
    char *p;
    if (streq (r, "*")) {
        *lo = tm_unit_min (u);
        *hi = tm_unit_max (u);
    }
    else if ((p = strchr (r, '-'))) {
        *(p++) = '\0';
        // r = lo, p = hi
        if (((*lo = tm_string2int (r, u)) < 0)
           || ((*hi = tm_string2int (p, u)) < 0))
            return -1;
    }
    else {
        *lo = *hi = tm_string2int (r, u);
        if (*lo < 0)
            return -1;
    }
    return 0;
}

static int range_parse (struct idset *n, tm_unit_t u, const char *range)
{
    char *cpy, *a1, *q, *s, *saveptr;
    int hi, lo;
    int rc = -1;

    if (!(cpy = strdup (range)))
        return -1;
    a1 = cpy;
    while ((s = strtok_r (a1, ",", &saveptr))) {
        int stride = 1;

        // check for a stride and nullify trailing "/":
        if ((q = strchr (s, '/'))) {
            *(q++) = '\0';
            if ((stride = string2int (q)) < 0)
                goto out;
        }

        // get next single range or number from string:
        if (get_range (s, u, &lo, &hi) < 0)
            goto out;

        if (lo == hi) {
            if (idset_set (n, lo) < 0)
                goto out;
        }
        else if (stride == 1) {
            if (idset_range_set (n, lo, hi) < 0)
                goto out;
        }
        else {
            while (lo <= hi) {
                if (idset_set (n, lo) < 0)
                    goto out;
                lo += stride;
            }
        }
        a1 = NULL;
    }
    rc = 0;
out:
    free (cpy);
    return rc;
}

int cronodate_set (cronodate_t *d, tm_unit_t item, const char *range)
{
    struct idset *n = d->item [item].set;
    assert (n != NULL);
    /*
     *  Delete all existing nodeset members before setting the new range.
     */
    if (idset_range_clear (n, tm_unit_min (item), tm_unit_max (item)) < 0)
        return -1;
    return range_parse (n, item, range);
}

int cronodate_set_integer (cronodate_t *d, tm_unit_t item, int value)
{
    struct idset *n = d->item [item].set;
    assert (n != NULL);
    if (value > tm_unit_max (item) || value < tm_unit_min (item)) {
        errno = ERANGE;
        return -1;
    }
    /*  Clear all members before setting the new value */
    if (idset_range_clear (n, tm_unit_min (item), tm_unit_max (item)) < 0)
        return -1;
    if (idset_set (n, value) < 0)
        return -1;
    return 0;
}

const char *cronodate_get (cronodate_t *d, tm_unit_t u)
{
    free (d->item[u].encoding);
    d->item[u].encoding = idset_encode (d->item[u].set, IDSET_FLAG_RANGE);
    return d->item[u].encoding;
}

void cronodate_fillset (cronodate_t *d)
{
    int i;
    for (i = 0; i < TM_MAX_ITEM; i++)
        (void)idset_range_set (d->item [i].set,
                               tm_unit_min (i),
                               tm_unit_max (i));
}

void cronodate_emptyset (cronodate_t *d)
{
    int i;
    for (i = 0; i < TM_MAX_ITEM; i++)
        (void)idset_range_clear (d->item [i].set,
                                 tm_unit_min (i),
                                 tm_unit_max (i));
}

/* Return pointer to item in struct tm that corresponds to tm_unit_t type.
 */
static int *tm_item (struct tm *t, tm_unit_t item)
{
    switch (item) {
        case TM_SEC:
            return &t->tm_sec;
        case TM_MIN:
            return &t->tm_min;
        case TM_HOUR:
            return &t->tm_hour;
        case TM_MDAY:
            return &t->tm_mday;
        case TM_MON:
            return &t->tm_mon;
        case TM_WDAY:
            return &t->tm_wday;
        case TM_YEAR:
            return &t->tm_year;
        default:
            break;
    }
    return NULL;
}

static void tm_incr (struct tm *tm, tm_unit_t item)
{
    int *ti = tm_item (tm, item);
    (*ti)++;
}

static void tm_set (struct tm *tm, tm_unit_t item, int val)
{
    int *ti = tm_item (tm, item);
    *ti = val;
}

/*
 *  Starting at item-1 reset all values of tm struct `tm`
 *   to their minimum values.
 */
static void tm_reset (struct tm *tm, tm_unit_t item)
{
    int i;
    for (i = item-1; i >= TM_SEC; i--)
        tm_set (tm, i, tm_unit_min (i));
}

/* advance time unit `item` in tm struct `tm` to new value `val`.
 *  if value is less than current value for the unit (e.g. for
 *  hours tm.tm_hour is 20 and we "advance" to 2), then increment
 *  the upper unit (e.g. mday), and reset all lower units to
 *  zero (e.g. minutes, seconds).
 */
static int tm_advance (struct tm *tm, tm_unit_t item, int val)
{
    int *ti;

    switch (item) {
    case TM_SEC:
    case TM_MIN:
    case TM_HOUR:
    case TM_MDAY:
    case TM_MON:
        ti = tm_item (tm, item);
        /* If current value is greater than new value, then roll over
         *  to the next higher time unit (e.g. for seconds add one to minutes)
         *  and set this unit to the new "val" value.
         */
        if (*ti > val)
            tm_incr (tm, item+1);
        *ti = val;
        tm_reset (tm, item);
        break;
    /* Year is special. It doesn't overflow
     */
    case TM_YEAR:
        tm->tm_year = val;
        tm_reset (tm, TM_YEAR);
        break;

    /* day of week is special */
    case TM_WDAY:
        if (tm->tm_wday > val) // into next week
            tm->tm_mday += (7 - tm->tm_wday) + val;
        else
            tm->tm_mday += val - tm->tm_wday;
        tm_reset (tm, TM_MDAY);
        break;
    default:
        return -1;
    }
    return 0;
}

bool cronodate_match (cronodate_t *d, struct tm *tm)
{
    int i;
    for (i = 0; i < TM_MAX_ITEM; i++) {
        struct idset *n = d->item [i].set;
        int *ti = tm_item (tm, i);
        if (!idset_test (n, *ti)) {
            return false;
        }
    }
    return true;
}

int cronodate_next (cronodate_t *d, struct tm *tm)
{
    int i;
    time_t t, now;
    if (tm == NULL || d == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Advance 1s into future so we get next future time
     *  and do not match "now".
     */
    tm->tm_sec++;
    if ((now = mktime (tm)) == (time_t) -1)
        return -1;

again:
    /* Loop through configured date-time units to see if
     *  they all fall within per-unit nodesets of `d`.
     *  for each that does not match, advance the time by
     *  one unit and try again.
     *
     * If time is advanced, check to sure we don't go more
     *  than a year into the future. This could be fixed up
     *  later, but for now is sufficient.
     */
    for (i = TM_SEC; i < TM_MAX_ITEM; i++) {
        struct idset *n = d->item [i].set;
        int *ti = tm_item (tm, i);

        if (!idset_test (n, *ti)) {
            unsigned int next;
            if ((next = idset_next (n, *ti)) == IDSET_INVALID_ID)
                next = idset_first (n);
            tm_advance (tm, i, next);
            // Call mktime to fix up any overflow, and check that
            //  don't iterate more than 2 years in the future
            if ((t = mktime (tm)) == (time_t) -1)
                return -1;
            if ((t - now) > 2*60*60*24*365) {
                errno = EOVERFLOW;
                return -1;
            }
            goto again;
        }
    }
    return 0;
}

double cronodate_remaining (cronodate_t *d, double now)
{
    struct tm tm;
    time_t t = (time_t) now;
    if (localtime_r (&t, &tm) == NULL)
        return -1.;
    if (cronodate_next (d, &tm) < 0)
        return -1.;
    if ((t = mktime (&tm)) == (time_t) -1)
        return -1.;
    return ((double) t - now);
}
 /* vi:tabstop=4 shiftwidth=4 expandtab
 */

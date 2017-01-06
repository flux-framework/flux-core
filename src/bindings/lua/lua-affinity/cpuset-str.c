/*****************************************************************************
 *
 *  Copyright (C) 2007-2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *
 *  UCRL-CODE-235358
 *
 *  This file is part of slurm-spank-plugins, a set of spank plugins for SLURM.
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

#define _GNU_SOURCE
#include <stdint.h> /* uint32_t   */
#include <stdlib.h> /* strtoul   */
#include <sched.h>  /* cpu_set_t */
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h> /* ULONG_MAX */
#include <inttypes.h>

char * cpuset_to_cstr (cpu_set_t *mask, char *str)
{
    int i;
    char *ptr = str;
    int entry_made = 0;

    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, mask)) {
            int j;
            int run = 0;
            entry_made = 1;
            for (j = i + 1; j < CPU_SETSIZE; j++) {
                if (CPU_ISSET(j, mask))
                    run++;
                else
                    break;
            }
            if (!run)
                sprintf(ptr, "%d,", i);
            else if (run == 1) {
                sprintf(ptr, "%d,%d,", i, i + 1);
                i++;
            } else {
                sprintf(ptr, "%d-%d,", i, i + run);
                i += run;
            }
            while (*ptr != 0)
                ptr++;
        }
    }
    ptr -= entry_made;
    *ptr = 0;

    return str;
}

static const char * nexttoken (const char *p, int sep)
{
    if (p)
        p = strchr (p, sep);
    if (p)
        p++;
    return (p);
}

static int cpuset_last_bit (cpu_set_t *mask)
{
    int i;
    for (i = CPU_SETSIZE - 1; i >= 0; --i)
        if (CPU_ISSET (i, mask)) return i;
    return (0);
}

#define HEXCHARSIZE  8   /*  8 chars per chunk */
#define HEXCHUNKSZ  32   /* 32 bits  per chunk */
#define NCHUNKS     (CPU_SETSIZE + (HEXCHUNKSZ-1))/HEXCHUNKSZ

/*
 *  hex_to_cpuset() and cpuset_to_hex() taken from libbitmask and
 *   modified to work with cpu_set_t:
 *
 * bitmask user library implementation.
 *
 * Copyright (c) 2004-2006 Silicon Graphics, Inc. All rights reserved.
 *
 * Paul Jackson <pj@sgi.com>
 */

#define max(a,b) ((a) > (b) ? (a) : (b))
int cpuset_to_hex (cpu_set_t *mask, char *str, size_t len)
{
    int chunk;
    int cnt = 0;
    int lastchunk = cpuset_last_bit (mask) / HEXCHUNKSZ;
    const char *sep = "";

    if (len <= 0)
        return 0;

    str[0] = 0;

    for (chunk = lastchunk; chunk >= 0; chunk--) {
        uint32_t val = 0;
        int bit;

        for (bit = HEXCHUNKSZ - 1; bit >= 0; bit--)
            val = val << 1 | CPU_ISSET (chunk * HEXCHUNKSZ + bit, mask);
        cnt += snprintf (str + cnt, max (len - cnt, 0), "%s%0*"PRIx32,
                sep, HEXCHARSIZE, val);

        sep = ",";
    }

    return cnt;
}

static inline int char_to_val (int c)
{
    int cl;

    cl = tolower(c);
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (cl >= 'a' && cl <= 'f')
        return cl + (10 - 'a');
    else
        return -1;
}

static int s_to_cpuset (cpu_set_t *mask, const char *str, int len)
{
    int base = 0;
    const char *ptr = str + len - 1;

    while (ptr >= str) {
        char val = char_to_val(*ptr);
        if (val == (char) -1)
            return -1;
        if (val & 1)
            CPU_SET(base, mask);
        if (val & 2)
            CPU_SET(base + 1, mask);
        if (val & 4)
            CPU_SET(base + 2, mask);
        if (val & 8)
            CPU_SET(base + 3, mask);
        len--;
        ptr--;
        base += 4;
    }

    return 0;
}


int hex_to_cpuset (cpu_set_t *mask, const char *str)
{
    const char *p, *q;
    int nchunks = 0, chunk;

    CPU_ZERO (mask);
    if (strlen(str) == 0)
        return 0;

    /*
     *  Skip any leading 0x
     */
    if (strncmp (str, "0x", 2) == 0)
        str += 2;

    q = str;

    while (p = q, q = nexttoken (q, ','), p)
        nchunks++;

    if (nchunks == 1)
        return s_to_cpuset (mask, str, strlen (str));

    chunk = nchunks - 1;
    q = str;


    while (p = q, q = nexttoken (q, ','), p) {
        uint32_t val;
        int bit;
        char *endptr;
        int nchars_read, nchars_unread;

        val = strtoul (p, &endptr, 16);

        nchars_read = endptr - p;
        if (nchars_read > HEXCHARSIZE) {
            /*  We overflowed val, have to do this chunk manually */
            if (s_to_cpuset (mask, p, endptr - p) < 0)
                goto err;
        }
        else {
            /* We should have consumed up to next comma,
             *   or if at last token, up until end of the string
             */
            nchars_unread = q - endptr;
            if ((q && nchars_unread != 1) || (!q && *endptr != '\0'))
                goto err;

            for (bit = HEXCHUNKSZ - 1; bit >= 0; bit--) {
                int n = chunk * HEXCHUNKSZ + bit;
                if (n >= CPU_SETSIZE)
                    goto err;
                if ((val >> bit) & 1)
                    CPU_SET (n, mask);
            }
        }
        chunk--;
    }
    return 0;
err:
    CPU_ZERO (mask);
    return -1;
}


int cstr_to_cpuset(cpu_set_t *mask, const char* str)
{
    const char *p, *q;
    char *endptr;
    q = str;
    CPU_ZERO(mask);

    if (strlen (str) == 0)
        return 0;

    while (p = q, q = nexttoken(q, ','), p) {
        unsigned long a; /* beginning of range */
        unsigned long b; /* end of range */
        unsigned long s; /* stride */
        const char *c1, *c2;

        a = strtoul(p, &endptr, 10);
        if (endptr == p)
            return EINVAL;
        if (a >= CPU_SETSIZE)
            return E2BIG;
        /*
         *  Leading zeros are an error:
         */
        if ((a != 0 && *p == '0') || (a == 0 && memcmp (p, "00", 2L) == 0))
            return 1;

        b = a;
        s = 1;

        c1 = nexttoken(p, '-');
        c2 = nexttoken(p, ',');
        if (c1 != NULL && (c2 == NULL || c1 < c2)) {

            /*
             *  Previous conversion should have used up all characters
             *     up to next '-'
             */
            if (endptr != (c1-1)) {
                return 1;
            }

            b = strtoul (c1, &endptr, 10);
            if (endptr == c1)
                return EINVAL;
            if (b >= CPU_SETSIZE)
                return E2BIG;

            c1 = nexttoken(c1, ':');
            if (c1 != NULL && (c2 == NULL || c1 < c2)) {
                s = strtoul (c1, &endptr, 10);
                if (endptr == c1)
                    return EINVAL;
                if (b >= CPU_SETSIZE)
                    return E2BIG;
            }
        }

        if (!(a <= b))
            return EINVAL;
        while (a <= b) {
            CPU_SET(a, mask);
            a += s;
        }
    }

    /*  Error if there are left over characters */
    if (endptr && *endptr != '\0')
        return EINVAL;

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */

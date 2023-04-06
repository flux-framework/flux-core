/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include <langinfo.h>
#include <locale.h>

#include "fluid.h"
#include "mnemonic.h"

/* fluid: [ts:40 id:14 seq:10] */
static const int bits_per_ts = 40;
static const int bits_per_id = 14;
static const int bits_per_seq = 10;

/* Max base58 string length for F58 encoding */
#define MAX_B58_STRLEN 12

#if ASSUME_BROKEN_LOCALE
static const char f58_prefix[] = "f";
static const char f58_alt_prefix[] = "ƒ";
#else
static const char f58_prefix[] = "ƒ";
static const char f58_alt_prefix[] = "f";
#endif /* ASSUME_BROKEN_LOCALE */

/*  b58digits_map courtesy of libbase58:
 *
 *  https://github.com/bitcoin/libbase58.git
 *

Copyright (c) 2014 Luke Dashjr

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

static const int8_t b58digits_map[] = {
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6,  7, 8,-1,-1,-1,-1,-1,-1,
    -1, 9,10,11,12,13,14,15, 16,-1,17,18,19,20,21,-1,
    22,23,24,25,26,27,28,29, 30,31,32,-1,-1,-1,-1,-1,
    -1,33,34,35,36,37,38,39, 40,41,42,43,-1,44,45,46,
    47,48,49,50,51,52,53,54, 55,56,57,-1,-1,-1,-1,-1,
};

static const char b58digits[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static int current_ds (uint64_t *ds)
{
    struct timespec ts;

    if (clock_gettime (CLOCK_MONOTONIC, &ts) < 0)
        return -1;
    *ds = ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
    return 0;
}

int fluid_init (struct fluid_generator *gen, uint32_t id, uint64_t timestamp)
{
    if (current_ds (&gen->clock_zero) < 0)
        return -1;
    if (id >= (1ULL<<bits_per_id))
        return -1;
    gen->id = id;
    gen->seq = 0;
    gen->clock_offset = timestamp;
    gen->timestamp = timestamp;
    return 0;
}

static int update_timestamp (struct fluid_generator *gen)
{
    uint64_t clock;
    uint64_t timestamp;

    if (current_ds (&clock) < 0)
        return -1;
    timestamp = clock - gen->clock_zero + gen->clock_offset;
    if (timestamp >= (1ULL<<bits_per_ts))
        return -1; // (unlikely) lifetime of FLUID sequence is over
    if (timestamp > gen->timestamp) {
        gen->seq = 0;
        gen->timestamp = timestamp;
    }
    return 0;
}

int fluid_save_timestamp (struct fluid_generator *gen, uint64_t *timestamp)
{
    if (update_timestamp (gen) < 0)
        return -1;
    *timestamp = gen->timestamp;
    return 0;
}

uint64_t fluid_get_timestamp (fluid_t fluid)
{
    return fluid >> (bits_per_seq + bits_per_id);
}

/* If sequence bits were exhausted (already 1024 allocated in this timestamp),
 * busy-wait, calling update_timestamp() until seq is cleared.
 * The busy-wait time is bounded by the timestamp quanta (1 msec).
 */
int fluid_generate (struct fluid_generator *gen, fluid_t *fluid)
{
    do {
        if (update_timestamp (gen) < 0)
            return -1;
    } while (gen->seq + 1 >= (1ULL<<bits_per_seq));
    *fluid = (gen->timestamp << (bits_per_seq + bits_per_id)
                    | (gen->id << bits_per_seq)
                    | gen->seq);
    gen->seq++;
    return 0;
}

/*  F58 encoding.
 */
/*  Compute base58 encoding of id in *reverse*
 *  Return number of characters written into buf
 */
static int b58revenc (char *buf, int bufsz, fluid_t id)
{
    int index = 0;
    memset (buf, 0, bufsz);
    if (id == 0) {
        buf[0] = b58digits[0];
        return 1;
    }
    while (id > 0) {
        int rem = id % 58;
        buf[index++] = b58digits[rem];
        id = id / 58;
    }
    return index;
}

static inline int is_utf8_locale (void)
{
    /* Check for UTF-8, only if the current encoding is multibyte (not C.UTF-8
     * or similar), but allow ascii encoding to be enforced if
     * FLUX_F58_FORCE_ASCII is set.
     */
    if (MB_CUR_MAX > 1 && !strcmp (nl_langinfo (CODESET), "UTF-8")
        && !getenv ("FLUX_F58_FORCE_ASCII"))
        return 1;
    return 0;
}

static int fluid_f58_encode (char *buf, int bufsz, fluid_t id)
{
    int count;
    const char *prefix = f58_prefix;
    char b58reverse[13];
    int index = 0;

    if (buf == NULL || bufsz <= 0) {
        errno = EINVAL;
        return -1;
    }

#if !ASSUME_BROKEN_LOCALE
    /* Use alternate "f" prefix if locale is not multibyte */
    if (!is_utf8_locale())
        prefix = f58_alt_prefix;
#endif

    if (bufsz <= strlen (prefix) + 1) {
        errno = EOVERFLOW;
        return -1;
    }
    if ((count = b58revenc (b58reverse, sizeof (b58reverse), id)) < 0) {
        errno = EINVAL;
        return -1;
    }

    /* Copy prefix to buf and zero remaining bytes */
    (void) strncpy (buf, prefix, bufsz);
    index = strlen (buf);

    if (index + count + 1 > bufsz) {
        errno = EOVERFLOW;
        return -1;
    }

    for (int i = count - 1; i >= 0; i--)
        buf[index++] = b58reverse[i];

    return 0;
}

static int b58decode (const char *str, uint64_t *idp)
{
    int64_t id = 0;
    int64_t scale = 1;
    int len = strlen (str);
    if (len == 0) {
        errno = EINVAL;
        return -1;
    }
    for (int i = len - 1; i >= 0; i--) {
        int8_t c = b58digits_map[(int8_t)str[i]];
        if (c == -1) {
            /* invalid base58 digit */
            errno = EINVAL;
            return -1;
        }
        id += c * scale;
        scale *= 58;
    }
    *idp = id;
    return 0;
}

static int fluid_is_f58 (const char *str)
{
    int len = 0;
    if (str == NULL || str[0] == '\0')
        return 0;
    len = strlen (f58_prefix);
    if (strncmp (str, f58_prefix, len) == 0)
        return len;
    len = strlen (f58_alt_prefix);
    if (strncmp (str, f58_alt_prefix, len) == 0)
        return len;
    return 0;
}

static int fluid_f58_decode (fluid_t *idptr, const char *str)
{
    int prefix = 0;
    const char *b58str = NULL;

    if (idptr == NULL || str == NULL) {
        errno = EINVAL;
        return -1;
    }
    if ((prefix = fluid_is_f58 (str)) == 0) {
        /* no prefix match, not valid f58 */
        errno = EINVAL;
        return -1;
    }
    b58str = str+prefix;
    if (strlen (b58str) > MAX_B58_STRLEN) {
        errno = EINVAL;
        return -1;
    }
    return b58decode (str+prefix, idptr);
}

static int fluid_decode_dothex (const char *s, fluid_t *fluid)
{
    int i;
    char *endptr;
    uint64_t b[4];

    for (i = 0; i < 4; i++) {
        b[i] = strtoul (i == 0 ? s : endptr + 1, &endptr, 16);
        if (i < 3 && *endptr != '.')
            return -1;
        if (i == 3 && *endptr != '\0')
            return -1;
    }
    *fluid = (b[0] << 48) | (b[1] << 32) | (b[2] << 16) | b[3];
    return 0;
}

static int fluid_encode_dothex (char *buf, int bufsz, fluid_t fluid)
{
    int rc;

    rc = snprintf (buf, bufsz, "%04x.%04x.%04x.%04x",
                   (unsigned int)(fluid>>48) & 0xffff,
                   (unsigned int)(fluid>>32) & 0xffff,
                   (unsigned int)(fluid>>16) & 0xffff,
                   (unsigned int)fluid & 0xffff);
    if (rc < 0 || rc >= bufsz)
        return -1;
    return 0;
}

int fluid_encode (char *buf, int bufsz, fluid_t fluid,
                  fluid_string_type_t type)
{
    switch (type) {
        case FLUID_STRING_DOTHEX:
            if (fluid_encode_dothex (buf, bufsz, fluid) < 0)
                return -1;
            break;
        case FLUID_STRING_MNEMONIC:
            if (mn_encode ((void *)&fluid, sizeof (fluid_t),
                            buf, bufsz, MN_FDEFAULT) != MN_OK)
                return -1;
            break;
        case FLUID_STRING_F58:
            if (fluid_f58_encode (buf, bufsz, fluid) < 0)
                return -1;
            break;
    }
    return 0;
}

static int fluid_validate (fluid_t fluid)
{
    unsigned long long ts = fluid >> (bits_per_seq + bits_per_id);
    unsigned int id = (fluid >> bits_per_seq) & ((1<<bits_per_id) - 1);
    unsigned int seq = fluid & ((1<<bits_per_seq) - 1);

    if (ts >= (1ULL<<bits_per_ts))
        return -1;
    if (id >= (1<<bits_per_id))
        return -1;
    if (seq >= (1<<bits_per_seq))
        return -1;
    return 0;
}

int fluid_decode (const char *s, fluid_t *fluidp, fluid_string_type_t type)
{
    int rc;
    fluid_t fluid;

    switch (type) {
        case FLUID_STRING_DOTHEX: {
            if (fluid_decode_dothex (s, &fluid) < 0)
                return -1;
            break;
        }
        case FLUID_STRING_MNEMONIC:
            /* N.B. Contrary to its inline documentation, mn_decode() returns
             * the number of bytes written to output, or MN_EWORD (-7).
             * Fluids are always encoded such that 8 bytes should be written.
             * Also, 's' is not modified so it is safe to cast away const.
             */
            rc = mn_decode ((char *)s, (void *)&fluid, sizeof (fluid_t));
            if (rc != 8) {
                errno = EINVAL;
                return -1;
            }
            break;
        case FLUID_STRING_F58:
            if (fluid_f58_decode (&fluid, s) < 0)
                return -1;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    if (fluid_validate (fluid) < 0)
        return -1;
    *fluidp = fluid;
    return 0;
}

static bool fluid_is_dothex (const char *s)
{
    return (strchr (s, '.') != NULL);
}

static bool fluid_is_words (const char *s)
{
    return (strchr (s, '-') != NULL);
}

fluid_string_type_t fluid_string_detect_type (const char *s)
{
    /* N.B.: An F58 encoded FLUID may start with 'f', which also could
     *  be true for dothex or words representations. Therefore, always
     *  check for these encodings first, since F58 must not have '.'
     *  or '-' characters, which distinguish dothex and mnemonic.
     */
    if (fluid_is_dothex (s))
        return FLUID_STRING_DOTHEX;
    if (fluid_is_words (s))
        return FLUID_STRING_MNEMONIC;
    if (fluid_is_f58 (s) > 0)
        return FLUID_STRING_F58;
    return 0;
}

static bool is_trailing_space (const char *p)
{
    while (*p != '\0' && isspace (*p))
        p++;
    return (*p == '\0');
}

int fluid_parse (const char *s, fluid_t *fluidp)
{
    int base = 10;
    unsigned long long l;
    char *endptr;
    fluid_string_type_t type;

    if (s == NULL || s[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    /*  Skip leading whitespace
     */
    while (*s != '\0' && isspace (*s))
        s++;

    if ((type = fluid_string_detect_type (s)) != 0)
        return fluid_decode (s, fluidp, type);

    /* O/w, FLUID encoded as an integer, either base16 (prefix="0x")
     *  or base10 (no prefix).
     */
    if (strncmp (s, "0x", 2) == 0)
        base = 16;
    errno = 0;
    l = strtoull (s, &endptr, base);
    if (errno != 0)
        return -1;
    /*  Ignore trailing whitespace */
    if (!is_trailing_space(endptr)) {
        errno = EINVAL;
        return -1;
    }
    *fluidp = l;

    if (fluid_validate (*fluidp) < 0)
        return -1;

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

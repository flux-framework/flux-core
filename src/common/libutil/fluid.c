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

#include "fluid.h"
#include "mnemonic.h"

/* fluid: [ts:40 id:14 seq:10] */
static const int bits_per_ts = 40;
static const int bits_per_id = 14;
static const int bits_per_seq = 10;

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

        default:
            errno = EINVAL;
            return -1;
    }
    if (fluid_validate (fluid) < 0)
        return -1;
    *fluidp = fluid;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

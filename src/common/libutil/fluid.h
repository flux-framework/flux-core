/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_FLUID_H
#define _UTIL_FLUID_H

#include <stdint.h>

/* FLUID - Flux Locally unique ID (64 bits):
 * - timestamp (ms since epoch):  40 bits (35 year long runtime)
 * - generator ID:                14 bits (up to 16K generators)
 * - sequence number:             10 bits (1024 IDs per ms)
 */

typedef enum {
    FLUID_STRING_DOTHEX = 1,    // x.x.x.x
    FLUID_STRING_MNEMONIC = 2,  // mnemonicode x-x-x--x-x-x
    FLUID_STRING_F58 = 3,       // FLUID base58 enc: ƒXXXX or fXXXX
    FLUID_STRING_EMOJI = 4,     // FLUID basemoji enc: 😪🏭🐭🍑👨
    FLUID_STRING_F58_PLAIN = 5, // FLUID base58 enc: fXXXX
} fluid_string_type_t;

struct fluid_generator {
    uint16_t id;
    uint16_t seq;
    uint64_t clock_zero;        // local clock value at fluid_init()
    uint64_t clock_offset;      // clock offset due to starting timestamp
    uint64_t timestamp;
};

typedef uint64_t fluid_t;

/* Initialize generator 'id' with starting 'timestamp'.
 * Returns 0 on success, -1 on failure.
 * Failures include id out of range, clock_gettime() error.
 */
int fluid_init (struct fluid_generator *gen, uint32_t id, uint64_t timestamp);

/* Returns 0 on success, -1 on failure.
 * Failures include timestamp out of range, clock_gettime() error.
 * N.B. may occasionally call usleep(3) for up to 1 ms to throttle
 * demand on each generator to at most 1024 FLUID's per ms.
 */
int fluid_generate (struct fluid_generator *gen, fluid_t *fluid);

/* Update and retrieve the internal timestamp.
 * Returns 0 on success, -1 on failure.
 */
int fluid_save_timestamp (struct fluid_generator *gen, uint64_t *timestamp);

/* Extract timestamp from a fluid.
 */
uint64_t fluid_get_timestamp (fluid_t fluid);

/* Convert 'fluid' to NULL-terminated string 'buf' of specified type.
 * Return 0 on success, -1 on failure.
 */
int fluid_encode (char *buf, int bufsz, fluid_t fluid,
                  fluid_string_type_t type);

/* Convert NULL-terminated string 's' of specified 'type' to 'fluid'.
 * Return 0 on success, -1 on failure.
 */
int fluid_decode (const char *s, fluid_t *fluid,
                  fluid_string_type_t type);

/* Attempt to detect the string type of encoded FLUID in `s`.
 *  returns the string type or 0 if not one the defined encodings above.
 *  (FLUID may still be encoded as integer in decimal or hex)
 */
fluid_string_type_t fluid_string_detect_type (const char *s);

/* Convert NULL-terminated string 's' to 'fluid' by auto-detecting
 *  the encoding in 's'.
 * Supported encodings include any fluid_string_type_t, or an integer
 *  in decimal or hexadecimal prefixed with "0x".
 * Return 0 on success, -1 on failure.
 */
int fluid_parse (const char *s, fluid_t *fluid);

#endif /* !_UTIL_FLUID_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

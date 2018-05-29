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
} fluid_string_type_t;

struct fluid_generator {
    uint16_t id;
    uint64_t epoch;
    uint16_t seq;
    uint64_t last_ds;
};

typedef uint64_t fluid_t;

/* Returns 0 on success, -1 on failure.
 * Failures include id out of range, clock_gettime() error.
 */
int fluid_init (struct fluid_generator *gen, uint32_t id);

/* Returns 0 on success, -1 on failure.
 * Failures include timestamp out of range, clock_gettime() error.
 * N.B. may occasionally call usleep(3) for up to 1 ms to throttle
 * demand on each generator to at most 1024 FLUID's per ms.
 */
int fluid_generate (struct fluid_generator *gen, fluid_t *fluid);

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

#endif /* !_UTIL_FLUID_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

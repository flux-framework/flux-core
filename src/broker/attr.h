/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef BROKER_ATTR_H
#define BROKER_ATTR_H

#include <stdint.h>
#include <flux/core.h>

enum {
    ATTR_IMMUTABLE      = 0x01, // value never changes once set
    ATTR_READONLY       = 0x02, // value is not to be set on cmdline by users
    ATTR_RUNTIME        = 0x04, // value may be updated by users [unused]
    ATTR_CONFIG         = 0x08, // value overrides TOML config [unused]
};

/* Callbacks for active values.  Return 0 on success, -1 on error with
 * errno set.  Errors are propagated to the return of attr_set() and attr_get().
 */
typedef int (*attr_get_f)(const char *name, const char **val, void *arg);
typedef int (*attr_set_f)(const char *name, const char *val, void *arg);

typedef struct broker_attr attr_t;

/* Create/destroy attribute cache
 */
attr_t *attr_create (void);
void attr_destroy (attr_t *attrs);

/* Register message handlers
 */
int attr_register_handlers (attr_t *attrs, flux_t *h);

/* Delete an attribute
 */
int attr_delete (attr_t *attrs, const char *name, bool force);

/* Add an attribute
 */
int attr_add (attr_t *attrs, const char *name, const char *val, int flags);

/* Helper functions to add a non-string attribute.  It performs the conversion
 *  to a string on the caller's behalf.
 */
int attr_add_int (attr_t *attrs, const char *name, int val, int flags);
int attr_add_uint32 (attr_t *attrs,
                     const char *name,
                     uint32_t val,
                     int flags);

/* Get/set an attribute.
 */
int attr_get (attr_t *attrs, const char *name, const char **val, int *flags);

int attr_set (attr_t *attrs, const char *name, const char *val);

/* Set an attribute's flags.
 */
int attr_set_flags (attr_t *attrs, const char *name, int flags);

/* Add an attribute with callbacks for get/set.
 */
int attr_add_active (attr_t *attrs,
                     const char *name,
                     int flags,
                     attr_get_f get,
                     attr_set_f set,
                     void *arg);

/* Add an attribute that tracks an integer value
 */
int attr_add_active_int (attr_t *attrs,
                         const char *name,
                         int *val,
                         int flags);
int attr_add_active_uint32 (attr_t *attrs,
                            const char *name,
                            uint32_t *val,
                            int flags);

/* Get an attribute and parse it as an integer value.
 */
int attr_get_uint32 (attr_t *attrs, const char *name, uint32_t *value);

/* Iterate over attribute names with internal cursor.
 */
const char *attr_first (attr_t *attrs);
const char *attr_next (attr_t *attrs);

/* Cache all immutable attributes present in attrs at this point in time.
 */
int attr_cache_immutables (attr_t *attrs, flux_t *h);

#endif /* BROKER_ATTR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

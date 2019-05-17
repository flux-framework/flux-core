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
#    define BROKER_ATTR_H

#    include <stdint.h>
#    include <flux/core.h>

enum {
    FLUX_ATTRFLAG_IMMUTABLE = 1, /* attribute is cacheable */
    FLUX_ATTRFLAG_READONLY = 2,  /* attribute cannot be written */
                                 /*   but may change on broker */
    FLUX_ATTRFLAG_ACTIVE = 4,    /* attribute has get and/or set callbacks */
};

/* Callbacks for active values.  Return 0 on succes, -1 on eror with
 * errno set.  Errors are propagated to the return of attr_set() and attr_get().
 */
typedef int (*attr_get_f) (const char *name, const char **val, void *arg);
typedef int (*attr_set_f) (const char *name, const char *val, void *arg);

typedef struct attr_struct attr_t;

/* Create/destroy attribute cache
 */
attr_t *attr_create (void);
void attr_destroy (attr_t *attrs);

/* Register/unregister message handlers
 */
int attr_register_handlers (attr_t *attrs, flux_t *h);
void attr_unregister_handlers (attr_t *attrs);

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
int attr_add_uint32 (attr_t *attrs, const char *name, uint32_t val, int flags);

/* Get/set an attribute.
 */
int attr_get (attr_t *attrs, const char *name, const char **val, int *flags);

int attr_set (attr_t *attrs, const char *name, const char *val, bool force);

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
int attr_add_active_int (attr_t *attrs, const char *name, int *val, int flags);
int attr_add_active_uint32 (attr_t *attrs,
                            const char *name,
                            uint32_t *val,
                            int flags);

/* Iterate over attribute names with internal cursor.
 */
const char *attr_first (attr_t *attrs);
const char *attr_next (attr_t *attrs);

#endif /* BROKER_ATTR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

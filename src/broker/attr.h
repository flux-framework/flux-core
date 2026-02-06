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

#include <flux/core.h>

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
int attr_delete (attr_t *attrs, const char *name);

/* Get/set an attribute.
 */
int attr_get (attr_t *attrs, const char *name, const char **val);

int attr_set (attr_t *attrs, const char *name, const char *val);

int attr_set_cmdline (attr_t *attrs,
                      const char *name,
                      const char *val,
                      flux_error_t *errp);

/* Add an attribute with callbacks for get/set.
 */
int attr_add_active (attr_t *attrs,
                     const char *name,
                     attr_get_f get,
                     attr_set_f set,
                     void *arg);

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

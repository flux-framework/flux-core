/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_AUX_H
#    define _UTIL_AUX_H

/* aux container - associate auxiliary data with a host object
 *
 * The object declares a 'struct aux_item *aux', initialized to NULL.
 * The object's destructor calls aux_destroy (&aux).
 * The object's aux_get/aux_set functions call aux_get ()/aux_set () below.
 *
 * An empty aux list is represented by a NULL pointer.
 *
 * It is legal to aux_set (key=NULL, value!=NULL).  aux_get (key=NULL) fails,
 * but aux_destroy () calls the anonymous value's destructor, if any.
 *
 * It is legal to aux_set () a duplicate key.  The new value replaces the old,
 * after calling its destructor, if any.
 *
 * It is legal to aux_set (key!=NULL, value=NULL).  Any value previously
 * stored under key is deleted, calling its destructor, if any.
 */

typedef void (*aux_free_f) (void *arg);

struct aux_item;

int aux_set (struct aux_item **aux,
             const char *key,
             void *val,
             aux_free_f free_fn);

void *aux_get (struct aux_item *aux, const char *key);

void aux_destroy (struct aux_item **aux);

#endif /* !_UTIL_AUX_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

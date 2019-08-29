/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_PLUGSTACK_H
#define _SHELL_PLUGSTACK_H

#include <jansson.h>

struct plugstack;
struct splugin;

struct plugstack * plugstack_create (void);

void plugstack_destroy (struct plugstack *st);

struct splugin *splugin_create (void);

void splugin_destroy (struct splugin *sp);

json_t *splugin_conf (struct splugin *sp);

int splugin_set_name (struct splugin *sp, const char *name);

int splugin_set_sym (struct splugin *sp, const char *symbol, void *sym);

void *splugin_get_sym (struct splugin *sp, const char *name);

int plugstack_push (struct plugstack *st, struct splugin *sp);

int plugstack_call (struct plugstack *st, const char *name, int nargs, ...);


#endif /* !_SHELL_PLUGSTACK_H */

/* vi: ts=4 sw=4 expandtab
 */


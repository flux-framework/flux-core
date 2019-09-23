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

#include <flux/core.h>

struct plugstack * plugstack_create (void);

void plugstack_destroy (struct plugstack *st);

/*  Set aux items to be propagated to any plugins loaded by
 *   plugstack_load().
 */
int plugstack_plugin_aux_set (struct plugstack *st,
                              const char *name,
                              void *val);

int plugstack_push (struct plugstack *st, flux_plugin_t *p);

/*  Set a colon-delimited set of PATHs to use when searching for
 *   a filename to load with plugstack_load().
 *  Replaces any previously set path.
 */
int plugstack_set_searchpath (struct plugstack *st, const char *searchpath);

/*  Get current plugstack searchpath
 */
const char *plugstack_get_searchpath (struct plugstack *st);

/*  Load all plugins matching glob pattern in all directories in
 *   searchpatch inst the plugin stack 'st', providing optional
 *   load configuration conf (a JSON encoded string).
 *
 *  If pattern starts with a '/' or '~' then searchpath is ignored.
 *  If searchpath is NULL then works like plugstack_load() below.
 *
 * Returns the number of plugins loaded or -1 on error.
 */
int plugstack_loadall (struct plugstack *st,
                       const char *searchpath,
                       const char *pattern,
                       const char *conf);

/*  Load all plugins matching a glob pattern, passing optional
 *   configuration conf (a JSON-encoded string).
 */
int plugstack_load (struct plugstack *st,
                    const char *pattern,
                    const char *conf);

int plugstack_call (struct plugstack *st,
                    const char *name,
                    flux_plugin_arg_t *args);

#endif /* !_SHELL_PLUGSTACK_H */

/* vi: ts=4 sw=4 expandtab
 */


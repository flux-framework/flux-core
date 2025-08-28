/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Check that 'path' is a directory owned by the instance owner
 * with at least owner=rwx permissions.
 */
int rundir_checkdir (const char *path, flux_error_t *error);

/* Create/check rundir or statedir (depending on the value of attr_name).
 */
int rundir_create (attr_t *attrs, const char *attr_name, flux_error_t *error);

// vi:ts=4 sw=4 expandtab

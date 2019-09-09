/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_INTREE_H
#define _UTIL_INTREE_H

/*  Check if the current executable was started from a build tree,
 *  i.e. if top_builddir is a prefix of the pat the the current
 *  executable.
 *
 *  Returns 1 if executable was started from the build tree, 0 if
 *  not and -1 on any error.
 */
int executable_is_intree (void);

/*  Return the directory containing the current executable.
 */
const char *executable_selfdir (void);

#endif /* !_UTIL_INTREE_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

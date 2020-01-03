/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_LIBUTIL_UNLINK_RECURSIVE_H
#define HAVE_LIBUTIL_UNLINK_RECURSIVE_H 1

/* Return number of files/directories unlinked or -1 on error.
 */
int unlink_recursive (const char *dirpath);

#endif /* !HAVE_LIBUTIL_UNLINK_RECURSIVE_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */


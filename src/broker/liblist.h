/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_BROKER_LIBLIST_H
#define HAVE_BROKER_LIBLIST_H 1

/* Create a list of candidate library paths to 'libname', using directories
 * from LD_LIBRARY_PATH, if any, plus parsed 'ldconfig -p' output.
 * Purpose: search for libpmi.so with the ability to detect a special
 * symbol in flux's version and skip over it, continuing the search.
 */
void liblist_destroy (zlist_t *libs);
zlist_t *liblist_create (const char *libname);

#endif /* !HAVE_BROKER_LIBLIST_H */

/*
 * vi:tabstop=4 shiftwidth
 */

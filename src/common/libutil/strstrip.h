/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_STRSTRIP_H
#define _UTIL_STRSTRIP_H

/*  Strip leading and trailing whitespace from string 's'.
 *  Returns a pointer inside of 's'.
 */
char *strstrip (char *s);

/*  Like strstrip(), but returns a copy of stripped 's'
 */
char *strstrip_copy (char *s);

#endif /* !_UTIL_STRSTRIP_H */

// vi:ts=4 sw=4 expandtab

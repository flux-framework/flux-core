/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SDBUS_OBJPATH_H
#define _SDBUS_OBJPATH_H

char *objpath_encode (const char *s);
char *objpath_decode (const char *s);

#endif /* !_SDBUS_OBJPATH_H */

// vi:ts=4 sw=4 expandtab

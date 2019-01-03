/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_SETENVF_H
#define _UTIL_SETENVF_H

int setenvf (const char *name, int overwrite, const char *fmt, ...);

#endif /* !_UTIL_SETENVF_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

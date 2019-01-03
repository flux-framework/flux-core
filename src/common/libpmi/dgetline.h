/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_LIBPMI_DGETLINE_H
#define _FLUX_LIBPMI_DGETLINE_H

int dgetline (int fd, char *buf, int len);
int dputline (int fd, const char *buf);

#endif /* !_FLUX_LIBPMI_DGETLINE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

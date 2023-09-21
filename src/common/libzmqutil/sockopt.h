/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ZMQUTIL_SOCKOPT_H
#define _ZMQUTIL_SOCKOPT_H

int zsetsockopt_int (void *sock, int option_name, int value);
int zgetsockopt_int (void *sock, int option_name, int *value);

int zsetsockopt_str (void *sock, int option_name, const char *value);
int zgetsockopt_str (void *sock, int option_name, char **value);

#endif /* !_ZMQUTIL_SOCKOPT_H */

// vi:ts=4 sw=4 expandtab

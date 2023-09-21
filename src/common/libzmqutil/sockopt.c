/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <zmq.h>
#include <string.h>

#include "sockopt.h"

int zsetsockopt_int (void *sock, int option_name, int value)
{
    return zmq_setsockopt (sock, option_name, &value, sizeof (value));
}

int zgetsockopt_int (void *sock, int option_name, int *value)
{
    int val;
    size_t size = sizeof (val);
    if (zmq_getsockopt (sock, option_name, &val, &size) < 0)
        return -1;
    *value = val;
    return 0;
}

int zgetsockopt_str (void *sock, int option_name, char **value)
{
    char val[1024];
    size_t size = sizeof (val);
    char *cpy;

    if (zmq_getsockopt (sock, option_name, &val, &size) < 0)
        return -1;
    if (!(cpy = strdup (val)))
        return -1;
    *value = cpy;
    return 0;
}

int zsetsockopt_str (void *sock, int option_name, const char *value)
{
    return zmq_setsockopt (sock, option_name, value, strlen (value));
}

// vi:ts=4 sw=4 expandtab

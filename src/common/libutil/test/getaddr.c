/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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
#include <limits.h>
#include <flux/core.h>

#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/log.h"


int main(int argc, char** argv)
{
    char buf[_POSIX_HOST_NAME_MAX + 1];
    flux_error_t error;
    char *name = getenv ("FLUX_IPADDR_INTERFACE");
    int flags = 0;

    log_init ("getaddr");

    if (argc > 2)
        log_msg_exit ("too many arguments");
    if (argc == 2)
        name = argv[1];
    if (getenv ("FLUX_IPADDR_HOSTNAME"))
        flags |= IPADDR_HOSTNAME;
    if (getenv ("FLUX_IPADDR_V6"))
        flags |= IPADDR_V6;

    if (ipaddr_getprimary (buf, sizeof (buf), flags, name, &error) < 0)
        log_msg_exit ("%s", error.text);

    printf ("%s\n", buf);
}

// vi:ts=4 sw=4 expandtab

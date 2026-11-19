/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* secure_getenv() replacement for platforms that lack it, e.g. macOS.
 *
 * Return NULL if the process may have been started with elevated
 * privileges (setuid, setgid, or file capabilities), otherwise behave
 * like getenv(3).
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <unistd.h>
#if HAVE_GETAUXVAL
#include <sys/auxv.h>
#endif

#include "secure_getenv.h"

static int is_secure_exec (void)
{
#if HAVE_ISSETUGID
    return issetugid ();
#elif HAVE_GETAUXVAL && defined(AT_SECURE)
    return getauxval (AT_SECURE) != 0;
#else
    /* Weaker fallback: this misses elevation via file capabilities,
     * and setuid programs that have since dropped privileges.
     */
    return getuid () != geteuid () || getgid () != getegid ();
#endif
}

char *secure_getenv (const char *name)
{
    if (is_secure_exec ())
        return NULL;
    return getenv (name);
}

// vi:ts=4 sw=4 expandtab

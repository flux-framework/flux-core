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

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

#include "sigutil.h"

struct signal_info {
    int signum;
    const char *name;
};

#define SIGDEF(x) { x, #x }

/* Note:
 *
 * This list was generated on Debian 11 with the cmdline:
 *
 *  $ kill -l \
 *      | sed 's/[0-9]*)//g' \
 *      | xargs -n1 printf '    SIGDEF(%s),\n' \
 *      | grep -v SIGRT
 */
static const struct signal_info signals[] = {
    SIGDEF(SIGHUP),
    SIGDEF(SIGINT),
    SIGDEF(SIGQUIT),
    SIGDEF(SIGILL),
    SIGDEF(SIGTRAP),
    SIGDEF(SIGABRT),
    SIGDEF(SIGBUS),
    SIGDEF(SIGFPE),
    SIGDEF(SIGKILL),
    SIGDEF(SIGUSR1),
    SIGDEF(SIGSEGV),
    SIGDEF(SIGUSR2),
    SIGDEF(SIGPIPE),
    SIGDEF(SIGALRM),
    SIGDEF(SIGTERM),
#ifdef SIGSTKFLT
    SIGDEF(SIGSTKFLT),
#endif
    SIGDEF(SIGCHLD),
    SIGDEF(SIGCONT),
    SIGDEF(SIGSTOP),
    SIGDEF(SIGTSTP),
    SIGDEF(SIGTTIN),
    SIGDEF(SIGTTOU),
    SIGDEF(SIGURG),
    SIGDEF(SIGXCPU),
    SIGDEF(SIGXFSZ),
    SIGDEF(SIGVTALRM),
    SIGDEF(SIGPROF),
    SIGDEF(SIGWINCH),
    SIGDEF(SIGIO),
#ifdef SIGPWR
    SIGDEF(SIGPWR),
#endif
    SIGDEF(SIGSYS),
};

static bool strisnumber (const char *s, int *result)
{
    char *endptr;
    long int l;

    errno = 0;
    l = strtol (s, &endptr, 10);
    if (errno || *endptr != '\0') {
        return false;
    }
    *result = (int) l;
    return true;
}

int sigutil_signum (const char *s)
{
    int signum;
    if (s == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (strisnumber (s, &signum)) {
        if (signum <= 0) {
            errno = EINVAL;
            return -1;
        }
        return signum;
    }
    for (int i = 0; i < ARRAY_SIZE(signals); i++) {
        if (streq (s, signals[i].name)
            || streq (s, signals[i].name+3))
            return signals[i].signum;
    }
    errno = ENOENT;
    return -1;
}

const char *sigutil_signame (int signum)
{
    if (signum <= 0) {
        errno = EINVAL;
        return NULL;
    }
    for (int i = 0; i < ARRAY_SIZE(signals); i++) {
        if (signals[i].signum == signum)
            return signals[i].name;
    }
    errno = ENOENT;
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

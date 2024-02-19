/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job common functions */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <pwd.h>

#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif

#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "ccan/str/str.h"
#include "common.h"

flux_jobid_t parse_jobid (const char *s)
{
    flux_jobid_t id;
    if (flux_job_id_parse (s, &id) < 0)
        log_msg_exit ("error parsing jobid: \"%s\"", s);
    return id;
}

/* Parse a free argument 's', expected to be a 64-bit unsigned.
 * On error, exit complaining about parsing 'name'.
 */
unsigned long long parse_arg_unsigned (const char *s, const char *name)
{
    unsigned long long i;
    char *endptr;

    errno = 0;
    i = strtoull (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0')
        log_msg_exit ("error parsing %s: \"%s\"", name, s);
    return i;
}

/* Parse free arguments into a space-delimited message.
 * On error, exit complaning about parsing 'name'.
 * Caller must free the resulting string
 */
char *parse_arg_message (char **argv, const char *name)
{
    char *argz = NULL;
    size_t argz_len = 0;
    error_t e;

    if ((e = argz_create (argv, &argz, &argz_len)) != 0)
        log_errn_exit (e, "error parsing %s", name);
    argz_stringify (argz, argz_len, ' ');
    return argz;
}

/* Parse an OPTPARSE_OPT_AUTOSPLIT list of state names, returning a
 * mask of states.  Exit with error if unknown state encountered.
 */
int parse_arg_states (optparse_t *p, const char *optname)
{
    int state_mask = 0;
    const char *arg;

    assert (optparse_hasopt (p, optname) == true);

    optparse_getopt_iterator_reset (p, optname);
    while ((arg = optparse_getopt_next (p, optname))) {
        flux_job_state_t state;

        if (flux_job_strtostate (arg, &state) == 0)
            state_mask |= state;
        else if (!strcasecmp (arg, "pending"))
            state_mask |= FLUX_JOB_STATE_PENDING;
        else if (!strcasecmp (arg, "running"))
            state_mask |= FLUX_JOB_STATE_RUNNING;
        else if (!strcasecmp (arg, "active"))
            state_mask |= FLUX_JOB_STATE_ACTIVE;
        else
            log_msg_exit ("error parsing --%s: %s is unknown", optname, arg);
    }
    if (state_mask == 0)
        log_msg_exit ("no states specified");
    return state_mask;
}

/* Parse user argument, which may be a username, a user id, or "all".
 * Print an error and exit if there is a problem.
 * Return numeric userid (all -> FLUX_USERID_UNKNOWN).
 */
uint32_t parse_arg_userid (optparse_t *p, const char *optname)
{
    uint32_t userid;
    const char *s = optparse_get_str (p, optname, NULL);
    struct passwd *pw;
    char *endptr;

    assert (s != NULL);
    if (streq (s, "all"))
        return FLUX_USERID_UNKNOWN;
    if ((pw = getpwnam (s)))
        return pw->pw_uid;
    errno = 0;
    userid = strtoul (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || !isdigit (*s))
        log_msg_exit ("unknown user %s", s);
    return userid;
}

char *trim_string (char *s)
{
    /* trailing */
    int len = strlen (s);
    while (len > 1 && isspace (s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
    /* leading */
    char *p = s;
    while (*p && isspace (*p))
        p++;
    return p;
}



/* vi: ts=4 sw=4 expandtab
 */

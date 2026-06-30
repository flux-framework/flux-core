/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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

#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <jansson.h>

#include "src/common/libutil/errprintf.h"

#include "auth.h"
#include "match.h"
#include "job_data.h"

struct job_auth {
    flux_t *h;
    bool private_mode;
};

static int config_parse_private_mode (struct job_auth *auth,
                                      const flux_conf_t *conf,
                                      flux_error_t *errp)
{
    int private_mode = 0;
    flux_error_t error;

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?{s?b}}",
                          "access",
                          "private-mode", &private_mode) < 0) {
        errprintf (errp,
                   "error reading [access] config: %s",
                   error.text);
        return -1;
    }

    auth->private_mode = private_mode ? true : false;
    return 0;
}

struct job_auth *job_auth_create (flux_t *h)
{
    struct job_auth *auth;
    flux_error_t error;

    if (!(auth = calloc (1, sizeof (*auth))))
        return NULL;
    auth->h = h;

    if (config_parse_private_mode (auth, flux_get_conf (h), &error) < 0) {
        flux_log (h, LOG_ERR, "%s", error.text);
        job_auth_destroy (auth);
        return NULL;
    }

    return auth;
}

void job_auth_destroy (struct job_auth *auth)
{
    if (auth) {
        int saved_errno = errno;
        free (auth);
        errno = saved_errno;
    }
}

int job_auth_config_reload (struct job_auth *auth,
                            const flux_conf_t *conf,
                            flux_error_t *errp)
{
    return config_parse_private_mode (auth, conf, errp);
}

static bool job_auth_restricted (struct job_auth *auth,
                                 struct flux_msg_cred cred)
{
    return auth->private_mode && !(cred.rolemask & FLUX_ROLE_OWNER);
}

bool job_auth_msg_restricted (struct job_auth *auth, const flux_msg_t *msg)
{
    struct flux_msg_cred cred;

    if (flux_msg_get_cred (msg, &cred) < 0)
        return true;
    if (job_auth_restricted (auth, cred)) {
        errno = EPERM;
        return true;
    }

    return false;
}

int job_auth_constraint (struct job_auth *auth,
                         const flux_msg_t *msg,
                         json_t **constraintp,
                         flux_error_t *errp)
{
    struct flux_msg_cred cred;

    if (flux_msg_get_cred (msg, &cred) < 0) {
        errprintf (errp, "failed to get message credential");
        return -1;
    }
    if (!job_auth_restricted (auth, cred)) {
        *constraintp = NULL;
        return 0;
    }

    if (!(*constraintp = json_pack ("{s:[i]}", "userid", cred.userid))) {
        errprintf (errp, "out of memory");
        errno = ENOMEM;
        return -1;
    }

    return 0;
}

int job_auth_check_job (struct job_auth *auth,
                        struct match_ctx *mctx,
                        const flux_msg_t *msg,
                        const struct job *job,
                        flux_error_t *errp)
{
    json_t *constraint = NULL;
    struct list_constraint *c;
    int ret;

    if (job_auth_constraint (auth, msg, &constraint, errp) < 0)
        return -1;

    if (!constraint)
        return 1;

    if (!(c = list_constraint_create (mctx, constraint, errp))) {
        json_decref (constraint);
        return -1;
    }
    json_decref (constraint);

    ret = job_match (job, c, errp);
    list_constraint_destroy (c);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

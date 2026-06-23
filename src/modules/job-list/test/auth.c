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
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/modules/job-list/auth.h"
#include "src/modules/job-list/match.h"
#include "src/modules/job-list/job_data.h"

static struct flux_msg_cred owner = { .userid = 1000,
                                      .rolemask = FLUX_ROLE_OWNER };
static struct flux_msg_cred guest = { .userid = 1234,
                                      .rolemask = FLUX_ROLE_USER };

/* Verify constraint is {"userid":[<uid>]} */
static int is_userid_constraint (json_t *c, int uid)
{
    json_t *ids;
    return json_is_object (c)
        && (ids = json_object_get (c, "userid"))
        && json_is_array (ids)
        && json_array_size (ids) == 1
        && json_integer_value (json_array_get (ids, 0)) == uid;
}

int main (int argc, char *argv[])
{
    flux_t *h;
    struct job_auth *auth;
    struct match_ctx *mctx;
    flux_msg_t *msg_owner = NULL;
    flux_msg_t *msg_guest = NULL;
    flux_conf_t *conf;
    flux_error_t error;
    json_t *o;
    struct job guest_job = { .userid = guest.userid };
    struct job owner_job = { .userid = owner.userid };

    plan (NO_PLAN);

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (!(mctx = match_ctx_create (h)))
        BAIL_OUT ("match_ctx_create failed");

    if (!(auth = job_auth_create (h)))
        BAIL_OUT ("job_auth_create failed");
    ok (true, "job_auth_create works with no [access] config");

    if (!(msg_owner = flux_msg_create (FLUX_MSGTYPE_REQUEST))
        || !(msg_guest = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");

    ok (flux_msg_set_cred (msg_owner, owner) == 0
        && flux_msg_set_cred (msg_guest, guest) == 0,
        "add owner and guest credentials to test request messages");

    errno = 0;
    ok (job_auth_msg_restricted (auth, NULL) && errno == EINVAL,
        "job_auth_msg_restricted (auth, NULL) rejects with EINVAL");

    /* private mode off (default): nobody is restricted */
    ok (!job_auth_msg_restricted (auth, msg_owner),
        "job_auth_msg_restricted: owner not restricted when private mode off");
    ok (!job_auth_msg_restricted (auth, msg_guest),
        "job_auth_msg_restricted: guest not restricted when private mode off");

    o = (json_t *)&error; /* non-NULL sentinel */
    ok (job_auth_constraint (auth, msg_guest, &o, &error) == 0 && o == NULL,
        "job_auth_constraint returns no constraint when private mode is off");

    ok (job_auth_check_job (auth, mctx, msg_guest, &owner_job, &error) == 1,
        "job_auth_check_job: guest can see any job when private mode is off");

    /* job_auth_config_reload: bad private-mode type */
    if (!(conf = flux_conf_pack ("{s:{s:s}}", "access", "private-mode", "yes")))
        BAIL_OUT ("flux_conf_pack failed");
    ok (job_auth_config_reload (auth, conf, &error) < 0,
        "job_auth_config_reload fails when private-mode is wrong type");
    ok (strlen (error.text) > 0,
        "error message is set on bad private-mode type");
    flux_conf_decref (conf);

    /* job_auth_config_reload: bad access table type */
    if (!(conf = flux_conf_pack ("{s:s}", "access", "not-a-table")))
        BAIL_OUT ("flux_conf_pack failed");
    ok (job_auth_config_reload (auth, conf, &error) < 0,
        "job_auth_config_reload fails when [access] is not a table");
    ok (strlen (error.text) > 0,
        "error message is set on bad [access] type");
    flux_conf_decref (conf);

    /* enable private mode via config reload */
    if (!(conf = flux_conf_pack ("{s:{s:b}}", "access", "private-mode", 1)))
        BAIL_OUT ("flux_conf_pack failed");
    ok (job_auth_config_reload (auth, conf, &error) == 0,
        "job_auth_config_reload enables private mode");
    flux_conf_decref (conf);

    /* private mode on: only non-owner is restricted */
    ok (!job_auth_msg_restricted (auth, msg_owner),
        "job_auth_msg_restricted: owner allowed with private mode enabled");
    ok (job_auth_msg_restricted (auth, msg_guest),
        "job_auth_msg_restricted: guest restricted with private mode enabled");

    o = (json_t *)&error; /* non-NULL sentinel */
    ok (job_auth_constraint (auth, msg_owner, &o, &error) == 0 && o == NULL,
        "owner gets no constraint when private mode is on");

    o = NULL;
    ok (job_auth_constraint (auth, msg_guest, &o, &error) == 0 && o != NULL,
        "guest gets a constraint when private mode is on");
    ok (is_userid_constraint (o, guest.userid),
        "guest constraint is {\"userid\":[<uid>]}");
    json_decref (o);

    ok (job_auth_check_job (auth, mctx, msg_owner, &owner_job, &error) == 1,
        "job_auth_check_job: owner can see any job in private mode");
    ok (job_auth_check_job (auth, mctx, msg_guest, &guest_job, &error) == 1,
        "job_auth_check_job: guest can see own job in private mode");
    ok (job_auth_check_job (auth, mctx, msg_guest, &owner_job, &error) == 0,
        "job_auth_check_job: guest cannot see other user's job in private mode");

    flux_msg_destroy (msg_owner);
    flux_msg_destroy (msg_guest);
    job_auth_destroy (auth);
    match_ctx_destroy (mctx);
    flux_close (h);

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

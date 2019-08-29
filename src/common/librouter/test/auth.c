/************************************************************  \
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/librouter/auth.h"

void lookup (void)
{
    uint32_t rolemask;
    flux_future_t *f;

    errno = 0;
    ok (auth_lookup_rolemask (NULL, 0) == NULL && errno == EINVAL,
        "auth_lookup_rolemask h=NULL fails with EINVAL");

    errno = 0;
    ok (auth_lookup_rolemask_get (NULL, &rolemask) < 0 && errno == EINVAL,
        "auth_lookup_rolemask_get f=NULL fails with EINVAL");

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    errno = 0;
    ok (auth_lookup_rolemask_get (NULL, &rolemask) < 0 && errno == EINVAL,
        "auth_lookup_rolemask_get f=NULL fails with EINVAL");

    flux_future_destroy (f);
}

int checkcred (const flux_msg_t *msg, struct auth_cred *cred)
{
    struct auth_cred msgcred;

    if (flux_msg_get_rolemask (msg, &msgcred.rolemask) < 0)
        BAIL_OUT ("flux_msg_get_rolemask faild");
    if (flux_msg_get_userid (msg, &msgcred.userid) < 0)
        BAIL_OUT ("flux_msg_get_userid faild");
    if (msgcred.rolemask != cred->rolemask)
        return -1;
    if (msgcred.userid != cred->userid)
        return -1;
    return 0;
}

void setcred (flux_msg_t *msg, struct auth_cred *cred)
{
    if (flux_msg_set_rolemask (msg, cred->rolemask) < 0)
        BAIL_OUT ("flux_msg_set_rolemask failed");
    if (flux_msg_set_userid (msg, cred->userid) < 0)
        BAIL_OUT ("flux_msg_set_userid failed");
}

void init_message (void)
{
    struct auth_cred nocred = { .userid = FLUX_USERID_UNKNOWN,
                                .rolemask = FLUX_ROLE_NONE };
    struct auth_cred ocred = { .userid = 0, .rolemask = FLUX_ROLE_OWNER };
    struct auth_cred gcred = { .userid = 42, .rolemask = FLUX_ROLE_USER };
    flux_msg_t *msg;

    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("flux_request_encode_failed");

    /* unint message gets connection creds (common case)
     */
    setcred (msg, &nocred);
    ok (auth_init_message (msg, &ocred) == 0 && checkcred (msg, &ocred) == 0,
        "auth_init_message conn=owner uninit message cred set to owner");

    setcred (msg, &nocred);
    ok (auth_init_message (msg, &gcred) == 0 && checkcred (msg, &gcred) == 0,
        "auth_init_message conn=guest uninit message cred set to guest");

    /* ability for connected owner message creds to pass through, not guest.
     */
    setcred (msg, &gcred);
    ok (auth_init_message (msg, &ocred) == 0 && checkcred (msg, &gcred) == 0,
        "auth_init_message conn=owner init message creds pass through");

    setcred (msg, &ocred);
    ok (auth_init_message (msg, &gcred) == 0 && checkcred (msg, &gcred) == 0,
        "auth_init_message conn=guest init message creds set to guest");

    /* invalid params
     */
    errno = 0;
    ok (auth_init_message (NULL, &ocred) < 0 && errno == EINVAL,
       "auth_init_message msg=NULL fails with EINVAL");

    errno = 0;
    ok (auth_init_message (msg, NULL) < 0 && errno == EINVAL,
       "auth_init_message cred=NULL fails with EINVAL");

    flux_msg_destroy (msg);
}

void event_privacy (void)
{
    struct auth_cred ocred = { .userid = 0, .rolemask = FLUX_ROLE_OWNER };
    struct auth_cred gcred = { .userid = 42, .rolemask = FLUX_ROLE_USER };
    struct auth_cred g2cred = { .userid = 43, .rolemask = FLUX_ROLE_USER };
    flux_msg_t *msg;

    if (!(msg = flux_event_encode ("foo", NULL)))
        BAIL_OUT ("flux_event_encode failed");

    /* public events visible to owner and guest
     */
    setcred (msg, &ocred);
    ok (auth_check_event_privacy (msg, &ocred) == 0,
       "auth_check_event_privacy conn=owner can see owner public event");
    setcred (msg, &gcred);
    ok (auth_check_event_privacy (msg, &ocred) == 0,
       "auth_check_event_privacy conn=owner can see guest public event");

    if (flux_msg_set_private (msg) < 0 || !flux_msg_is_private (msg))
        BAIL_OUT ("could not set message privacy flag");

    /* private events visible to owner
     */
    setcred (msg, &ocred);
    ok (auth_check_event_privacy (msg, &ocred) == 0,
       "auth_check_event_privacy conn=owner can see owner private event");
    setcred (msg, &gcred);
    ok (auth_check_event_privacy (msg, &ocred) == 0,
       "auth_check_event_privacy conn=owner can see guest private event");

    /* private event visibility to guests is limited to their own
     */
    setcred (msg, &ocred);
    errno = 0;
    ok (auth_check_event_privacy (msg, &gcred) < 0 && errno == EPERM,
       "auth_check_event_privacy conn=guest cannot see owner private event");

    setcred (msg, &g2cred);
    errno = 0;
    ok (auth_check_event_privacy (msg, &gcred) < 0 && errno == EPERM,
       "auth_check_event_privacy conn=guest cannot see guest2 private event");

    setcred (msg, &gcred);
    ok (auth_check_event_privacy (msg, &gcred) == 0,
       "auth_check_event_privacy conn=guest can see guest private event");

    /* invalid params
     */
    errno = 0;
    ok (auth_check_event_privacy (NULL, &ocred) < 0 && errno == EINVAL,
        "auth_check_event_privacy msg=NULL fails with EINVAL");

    errno = 0;
    ok (auth_check_event_privacy (msg, NULL) < 0 && errno == EINVAL,
        "auth_check_event_privacy cred=NULL fails with EINVAL");

    flux_msg_destroy (msg);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    lookup ();
    init_message ();
    event_privacy ();

    done_testing ();

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */

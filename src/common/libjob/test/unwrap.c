/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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
#if HAVE_FLUX_SECURITY
#include <flux/security/sign.h>
#endif

#include "src/common/libtap/tap.h"
#include "src/common/libjob/sign_none.h"
#include "src/common/libjob/unwrap.h"

typedef char * (*unwrap_f) (const char *s,
                            bool verify,
                            uint32_t *uidp,
                            flux_error_t *error);

static void test_api (unwrap_f unwrap)
{
    flux_error_t error;
    uint32_t userid;
    char *result;
    char *s;

    if (!(s = sign_none_wrap ("bar", 4, getuid ())))
        BAIL_OUT ("sign_none_wrap failed");

    userid = 0;
    memset (error.text, 0, sizeof (error.text));
    result = (*unwrap) (NULL, false, &userid, &error);
    ok (result == NULL,
        "unwrapp_string() fails with NULL argument");
    ok (userid == 0,
        "userid argument unmodified");
    ok (strlen (error.text),
        "error.text says: %s",
        error.text);

    userid = 0;
    result = (*unwrap) (s, false, NULL, &error);
    ok (result != NULL && userid == 0,
        "unwrap_string() works with NULL userid");
    if (result == NULL)
        diag ("got error: %s", error.text);
    is (result, "bar",
        "got expected result");
    free (result);

    userid = 0;
    result = (*unwrap) (s, false, NULL, NULL);
    ok (result != NULL && userid == 0,
        "unwrap_string() works with NULL userid and error parameters");
    is (result, "bar",
        "got expected result");
    free (result);

    userid = 0;
    result = (*unwrap) (s, true, NULL, NULL);
    ok (result != NULL && userid == 0,
        "unwrap_string() works with verify and NULL userid and error parameters");
    is (result, "bar",
        "got expected result");
    free (result);
    free (s);

    if (!(s = sign_none_wrap ("bar", 4, getuid () - 1)))
        BAIL_OUT ("sign_none_wrap failed");

    userid = 0;
    result = (*unwrap) (s, true, &userid, NULL);
    ok (result == NULL,
        "unwrap_string() fails with verify == true and errp == NULL");
    free (result);

    free (s);
}

/*  Test a good and bad payload (cribbed from test/sign_none.c)
 */
static void decode_bad_other (unwrap_f unwrap)
{
    const char *good = "dmVyc2lvbgBpMQB1c2VyaWQAaTEwMDAAbWVjaGFuaXNtAHNub25lAA==.Zm9vAA==.none";
    /* invalid base64 payload (% character) */
    const char *bad  = "dmVyc2lvbgBpMQB1c2VyaWQAaTEwMDAAbWVjaGFuaXNtAHNub25lAA==.%m9vAA==.none";

    flux_error_t error;
    uint32_t userid;
    char *result;

    /* Double check good input, the basis for bad input.
     *  (do not verify since uid will not match)
     */
    result = (*unwrap) (good, false, &userid, &error);
    ok (result != NULL,
        "unwrap_string() works for good sign-none payload");
    if (!result)
        diag ("%s", error.text);
    is (result, "foo",
        "result is %s", result);
    if (result)
        free (result);

    result = (*unwrap) (bad, false, &userid, &error);
    ok (result == NULL,
        "unwrap_string() fails on bad payload");
    diag ("%s", error.text);
    if (result)
        free (result);
}

static void unwrap_sign_none (unwrap_f unwrap)
{
    flux_error_t error;
    uint32_t userid;
    char *s;
    char *result;

    if (!(s = sign_none_wrap ("bar", 4, 1000)))
        BAIL_OUT ("sign_none_wrap failed");

    userid = 0;
    result = (*unwrap) (s, false, &userid, &error);
    ok (result != NULL && userid == 1000,
        "unwrap_string() after sign_none_wrap() works");
    is (result, "bar",
        "got expected result");
    free (result);
    free (s);


    if (!(s = sign_none_wrap ("bar", 3, 1000)))
        BAIL_OUT ("sign_none_wrap failed");
    userid = 0;
    result = (*unwrap) (s, false, &userid, &error);
    ok (result != NULL && userid == 1000,
        "unwrap_string() after sign_none_wrap() excluding NUL works");
    is (result, "bar",
        "got expected result");
    free (result);
    free (s);

}

#if HAVE_FLUX_SECURITY
static void sign_security (void)
{
    flux_security_t *sec;
    flux_error_t error;
    uint32_t userid;
    char *result;
    const char *s;

    if (!(sec = flux_security_create (0)))
        BAIL_OUT ("error creating flux-security context");
    if (flux_security_configure (sec, NULL) < 0)
        BAIL_OUT ("error configuring flux-security");

    s = flux_sign_wrap_as (sec, 1000, "foo", 4, "none", 0);
    if (!s) {
        BAIL_OUT ("flux_sign_wrap_as returned NULL: %s",
                  flux_security_last_error (sec));
    }

    userid = 0;
    result = unwrap_string (s, false, &userid, &error);
    ok (result && userid == 1000,
        "unwrap_string() from flux-security signer");
    free (result);

    /* valid userid */
    if (!(s = flux_sign_wrap_as (sec, getuid (), "foo", 4, "none", 0)))
        BAIL_OUT ("flux_sign_wrap_as returned NULL: %s",
                  flux_security_last_error (sec));
    userid = 0;
    result = unwrap_string (s, true, &userid, &error);
    ok (result && userid == getuid (),
        "unwrap_string() with verify = true works");
    free (result);

    /* Invalid userid */
    if (!(s = flux_sign_wrap_as (sec, getuid() - 1, "foo", 4, "none", 0)))
        BAIL_OUT ("flux_sign_wrap_as returned NULL: %s",
                  flux_security_last_error (sec));
    userid = 0;
    memset (error.text, 0, sizeof (error.text));
    result = unwrap_string (s, true, &userid, &error);
    ok (result == NULL,
        "unwrap_string() with verify = true and incorrect userid fails");
    ok (strlen (error.text),
        "unwrap_string() expected error: %s",
        error.text);
    free (result);

    /* Invalid userid (noverify) */
    userid = 0;
    memset (error.text, 0, sizeof (error.text));
    result = unwrap_string (s, false, &userid, &error);
    ok (result != NULL,
        "unwrap_string() with verify = false and incorrect userid succeeds");
    ok (userid == getuid() - 1,
        "unwrap_string() returned userid used for signing");
    ok (strlen (error.text) == 0,
        "unwrap_string() error.text still empty");
    free (result);


    flux_security_destroy (sec);
}
#endif

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_api (unwrap_string);
    test_api (unwrap_string_sign_none);

    decode_bad_other (unwrap_string);
    decode_bad_other (unwrap_string_sign_none);

    unwrap_sign_none (unwrap_string);
    unwrap_sign_none (unwrap_string_sign_none);
#if HAVE_FLUX_SECURITY
    sign_security ();
#endif

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

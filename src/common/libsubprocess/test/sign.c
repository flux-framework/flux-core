/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* sign.c - test RFC 42 request signing in subprocess server
 *
 * Tests require a subprocess server configured with require_sign=true.
 * The following cases are covered:
 *  - unsigned exec request is rejected (EPERM)
 *  - signed exec request from correct user succeeds
 *  - exec request signed as wrong userid is rejected (EPERM)
 *  - exec request with non-string signature field is rejected (EPROTO)
 *  - exec request with valid signature over non-JSON is rejected (EPROTO)
 *  - exec request with a tampered (corrupted) signature is rejected (EPERM)
 *  - exec request with valid signature but wrong topic is rejected (EPERM)
 *  - exec request with valid signature but missing field is rejected (EPROTO)
 *  - exec request with valid signature but wrong field type is rejected (EPROTO)
 *  - exec request with valid signature but wrong field type is rejected (EPROTO)
 *  - unsigned kill request is rejected (EPERM)
 *  - signed kill request succeeds
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <signal.h>
#include <flux/core.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/sign.h>
#endif

#include "ccan/str/str.h"
#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "src/common/libsubprocess/subprocess.h"
#include "src/common/libsubprocess/client.h"

#include "rcmdsrv.h"

#define SERVER_NAME "test-sign"

#if HAVE_FLUX_SECURITY

extern char **environ;

struct sign_ctx {
    flux_t *h;
    bool failed;
    bool completed;
};

static void discard_output_cb (flux_subprocess_t *p, const char *stream)
{
    const char *buf;
    (void)flux_subprocess_read (p, stream, &buf);
}

static void state_cb (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    struct sign_ctx *ctx = flux_subprocess_aux_get (p, "ctx");

    diag ("state: %s", flux_subprocess_state_string (state));
    if (state == FLUX_SUBPROCESS_FAILED) {
        ctx->failed = true;
        flux_reactor_stop (flux_get_reactor (ctx->h));
    }
}

static void completion_cb (flux_subprocess_t *p)
{
    struct sign_ctx *ctx = flux_subprocess_aux_get (p, "ctx");

    diag ("completion");
    ctx->completed = true;
    flux_reactor_stop (flux_get_reactor (ctx->h));
}

static flux_subprocess_ops_t sign_ops = {
    .on_completion   = completion_cb,
    .on_state_change = state_cb,
    .on_stdout       = discard_output_cb,
    .on_stderr       = discard_output_cb,
};

/* Run 'true' on the sign-required server with the given flags.
 * Returns true if subprocess completed, false if it failed.
 */
static bool run_true (flux_t *h, int flags)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p;
    struct sign_ctx ctx = { .h = h };

    if (!(cmd = flux_cmd_create (1, av, environ)))
        BAIL_OUT ("flux_cmd_create failed");
    p = flux_rexec_ex (h,
                       SERVER_NAME,
                       FLUX_NODEID_ANY,
                       flags,
                       cmd,
                       &sign_ops,
                       tap_logger,
                       NULL);
    if (!p)
        BAIL_OUT ("flux_rexec_ex failed");
    if (flux_subprocess_aux_set (p, "ctx", &ctx, NULL) < 0)
        BAIL_OUT ("flux_subprocess_aux_set failed");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        BAIL_OUT ("flux_reactor_run failed");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
    return ctx.completed;
}

static void test_unsigned_rejected (flux_t *h)
{
    ok (!run_true (h, 0),
        "unsigned exec to sign-required server fails");
}

static void test_signed_succeeds (flux_t *h)
{
    ok (run_true (h, FLUX_SUBPROCESS_FLAGS_SIGN),
        "signed exec to sign-required server succeeds");
}

/* Send a raw exec request signed as 'userid' and check for EPERM.
 * With the "none" mechanism, flux_sign_unwrap_anymech() rejects a token
 * that was signed for a different userid, so "signature verification failed"
 * is returned before the uid comparison check is reached.
 */
static void test_wrong_uid_rejected (flux_t *h)
{
    flux_security_t *sec;
    const char *token;
    char *topic = NULL;
    const char *errmsg;
    flux_future_t *f = NULL;
    /* Arbitrary content - uid check fires before payload is parsed */
    const char *content = "{}";
    int64_t wrong_uid = (int64_t)getuid () + 1;

    if (!(sec = subprocess_get_security (h)))
        BAIL_OUT ("subprocess_get_security failed");

    if (asprintf (&topic, "%s.exec", SERVER_NAME) < 0)
        BAIL_OUT ("asprintf failed");

    token = flux_sign_wrap_as (sec,
                               wrong_uid,
                               content,
                               strlen (content),
                               NULL,
                               0);
    skip (!token,
          3,
          "flux_sign_wrap_as as uid %ld not supported: %s",
          (long)wrong_uid,
          flux_security_last_error (sec))
        f = flux_rpc_pack (h,
                           topic,
                           FLUX_NODEID_ANY,
                           FLUX_RPC_STREAMING,
                           "{s:s}",
                           "signature", token);
        if (!f)
            BAIL_OUT ("flux_rpc_pack failed");
        ok (flux_future_get (f, NULL) < 0 && errno == EPERM,
            "exec signed as wrong uid gets EPERM");
        errmsg = flux_future_error_string (f);
        diag ("error: %s", errmsg ? errmsg : "(none)");
        ok (errmsg != NULL,
            "error string is set");
        like (errmsg, "signature verification failed",
              "error mentions signature verification failure");
    end_skip;
    flux_future_destroy (f);
    free (topic);
}

/* Send an exec request to the sign-required server with a token that was
 * created by signing 'content' as the current user.  Returns a new future.
 */
static flux_future_t *send_raw_signed (flux_t *h, const char *content)
{
    flux_security_t *sec;
    const char *token;
    char *topic = NULL;
    flux_future_t *f;

    if (!(sec = subprocess_get_security (h)))
        BAIL_OUT ("subprocess_get_security failed");
    if (asprintf (&topic, "%s.exec", SERVER_NAME) < 0)
        BAIL_OUT ("asprintf failed");
    token = flux_sign_wrap (sec, content, strlen (content), NULL, 0);
    if (!token)
        BAIL_OUT ("flux_sign_wrap failed: %s", flux_security_last_error (sec));
    f = flux_rpc_pack (h,
                       topic,
                       FLUX_NODEID_ANY,
                       FLUX_RPC_STREAMING,
                       "{s:s}",
                       "signature", token);
    if (!f)
        BAIL_OUT ("flux_rpc_pack failed");
    free (topic);
    return f;
}

/* The signature field exists but is not a JSON string. */
static void test_invalid_sig_type (flux_t *h)
{
    char *topic = NULL;
    const char *errmsg;
    flux_future_t *f;

    if (asprintf (&topic, "%s.exec", SERVER_NAME) < 0)
        BAIL_OUT ("asprintf failed");
    f = flux_rpc_pack (h,
                       topic,
                       FLUX_NODEID_ANY,
                       FLUX_RPC_STREAMING,
                       "{s:i}",
                       "signature", 42);
    if (!f)
        BAIL_OUT ("flux_rpc_pack failed");
    ok (flux_future_get (f, NULL) < 0 && errno == EPROTO,
        "exec with non-string signature field gets EPROTO");
    errmsg = flux_future_error_string (f);
    diag ("error: %s", errmsg ? errmsg : "(none)");
    ok (errmsg != NULL,
        "error string is set");
    like (errmsg, "signature field is not a string",
          "error mentions non-string signature field");
    flux_future_destroy (f);
    free (topic);
}

/* Signature is cryptographically valid but was created over non-JSON content,
 * so the signed payload cannot be parsed after successful verification.
 */
static void test_malformed_payload (flux_t *h)
{
    const char *errmsg;
    flux_future_t *f = send_raw_signed (h, "not-valid-json");

    ok (flux_future_get (f, NULL) < 0 && errno == EPROTO,
        "exec signed over non-JSON content gets EPROTO");
    errmsg = flux_future_error_string (f);
    diag ("error: %s", errmsg ? errmsg : "(none)");
    ok (errmsg != NULL,
        "error string is set");
    like (errmsg, "could not parse signed payload",
          "error mentions payload parse failure");
    flux_future_destroy (f);
}

/* Signature token has been tampered with (one byte flipped in the middle).
 * flux_sign_unwrap_anymech() should detect the corruption.
 */
static void test_tampered_signature (flux_t *h)
{
    flux_security_t *sec;
    const char *token;
    char *bad_token = NULL;
    char *topic = NULL;
    const char *errmsg;
    flux_future_t *f = NULL;
    const char *content = "{}";
    size_t len;

    if (!(sec = subprocess_get_security (h)))
        BAIL_OUT ("subprocess_get_security failed");
    if (asprintf (&topic, "%s.exec", SERVER_NAME) < 0)
        BAIL_OUT ("asprintf failed");
    token = flux_sign_wrap (sec, content, strlen (content), NULL, 0);
    if (!token)
        BAIL_OUT ("flux_sign_wrap failed: %s", flux_security_last_error (sec));
    if (!(bad_token = strdup (token)))
        BAIL_OUT ("strdup failed");
    /* Replace one character in the middle with a different valid base64
     * character, ensuring the token decodes but the signature doesn't verify.
     */
    len = strlen (bad_token);
    bad_token[len / 2] = (bad_token[len / 2] == 'A') ? 'B' : 'A';
    f = flux_rpc_pack (h,
                       topic,
                       FLUX_NODEID_ANY,
                       FLUX_RPC_STREAMING,
                       "{s:s}",
                       "signature", bad_token);
    if (!f)
        BAIL_OUT ("flux_rpc_pack failed");
    ok (flux_future_get (f, NULL) < 0 && errno == EPERM,
        "exec with tampered signature gets EPERM");
    errmsg = flux_future_error_string (f);
    diag ("error: %s", errmsg ? errmsg : "(none)");
    ok (errmsg != NULL,
        "error string is set");
    like (errmsg, "signature verification failed",
          "error mentions signature verification failure");
    flux_future_destroy (f);
    free (bad_token);
    free (topic);
}

/* Signature is valid and payload is valid JSON, but the topic embedded in the
 * signed payload does not match the actual request topic.  This tests replay
 * protection: a signature obtained for one endpoint cannot be used at another.
 */
static void test_topic_mismatch (flux_t *h)
{
    const char *errmsg;
    flux_future_t *f = send_raw_signed (h, "{\"topic\":\"other.exec\"}");

    ok (flux_future_get (f, NULL) < 0 && errno == EPERM,
        "exec with wrong topic in signed payload gets EPERM");
    errmsg = flux_future_error_string (f);
    diag ("error: %s", errmsg ? errmsg : "(none)");
    ok (errmsg != NULL,
        "error string is set");
    like (errmsg, "topic mismatch in signed payload",
          "error mentions topic mismatch");
    flux_future_destroy (f);
}

/* Signed payload is valid JSON with correct topic but is missing the
 * required 'flags' field.  The server should reject it with EPROTO after
 * successful signature verification.
 */
static void test_missing_required_field (flux_t *h)
{
    const char *errmsg;
    flux_future_t *f = send_raw_signed (h,
                                        "{\"topic\":\"" SERVER_NAME ".exec\","
                                        "\"cmd\":{\"cmdline\":[\"true\"],"
                                        "\"cwd\":\"/\",\"env\":{},"
                                        "\"opts\":{},\"channels\":[]}}");

    ok (flux_future_get (f, NULL) < 0 && errno == EPROTO,
        "exec with missing required field gets EPROTO");
    errmsg = flux_future_error_string (f);
    diag ("error: %s", errmsg ? errmsg : "(none)");
    like (errmsg, "invalid request payload",
          "error mentions invalid request payload");
    flux_future_destroy (f);
}

/* Signed payload has the correct topic but 'flags' is a string instead of
 * the required integer.  The server should reject it with EPROTO.
 */
static void test_wrong_field_type (flux_t *h)
{
    const char *errmsg;
    flux_future_t *f = send_raw_signed (h,
                                        "{\"topic\":\"" SERVER_NAME ".exec\","
                                        "\"cmd\":{\"cmdline\":[\"true\"],"
                                        "\"cwd\":\"/\",\"env\":{},"
                                        "\"opts\":{},\"channels\":[]},"
                                        "\"flags\":\"notanint\"}");

    ok (flux_future_get (f, NULL) < 0 && errno == EPROTO,
        "exec with flags as string instead of int gets EPROTO");
    errmsg = flux_future_error_string (f);
    diag ("error: %s", errmsg ? errmsg : "(none)");
    like (errmsg, "invalid request payload",
          "error mentions invalid request payload");
    flux_future_destroy (f);
}

/* Signed payload has the correct topic but 'cmd' is an integer instead of
 * the required object.  The server should reject it with EPROTO.
 */
static void test_cmd_wrong_type (flux_t *h)
{
    const char *errmsg;
    flux_future_t *f = send_raw_signed (h,
                                        "{\"topic\":\"" SERVER_NAME ".exec\","
                                        "\"cmd\":42,\"flags\":0}");

    ok (flux_future_get (f, NULL) < 0 && errno == EPROTO,
        "exec with cmd as int instead of object gets EPROTO");
    errmsg = flux_future_error_string (f);
    diag ("error: %s", errmsg ? errmsg : "(none)");
    like (errmsg, "error parsing command",
          "error mentions command parse failure");
    flux_future_destroy (f);
}

struct kill_ctx {
    flux_t *h;
    bool started;
    bool failed;
    bool completed;
    pid_t pid;
};

static void kill_state_cb (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    struct kill_ctx *ctx = flux_subprocess_aux_get (p, "ctx");

    diag ("state: %s", flux_subprocess_state_string (state));
    if (state == FLUX_SUBPROCESS_RUNNING) {
        ctx->started = true;
        ctx->pid = flux_subprocess_pid (p);
        flux_reactor_stop (flux_get_reactor (ctx->h));
    }
    else if (state == FLUX_SUBPROCESS_FAILED) {
        ctx->failed = true;
        flux_reactor_stop (flux_get_reactor (ctx->h));
    }
}

static void kill_completion_cb (flux_subprocess_t *p)
{
    struct kill_ctx *ctx = flux_subprocess_aux_get (p, "ctx");

    diag ("completion");
    ctx->completed = true;
    flux_reactor_stop (flux_get_reactor (ctx->h));
}

static flux_subprocess_ops_t kill_ops = {
    .on_completion   = kill_completion_cb,
    .on_state_change = kill_state_cb,
    .on_stdout       = discard_output_cb,
    .on_stderr       = discard_output_cb,
};

/* Start 'sleep 300' on the sign-required server with FLUX_SUBPROCESS_FLAGS_SIGN.
 * Blocks the reactor until the process reaches RUNNING state.
 * Fills in ctx->pid and ctx->started on success.
 * Returns the subprocess handle; caller must run the reactor once more to
 * collect completion, then call flux_subprocess_destroy().
 */
static flux_subprocess_t *start_sleep (flux_t *h, struct kill_ctx *ctx)
{
    char *av[] = { "sleep", "300", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p;

    if (!(cmd = flux_cmd_create (2, av, environ)))
        BAIL_OUT ("flux_cmd_create failed");
    p = flux_rexec_ex (h,
                       SERVER_NAME,
                       FLUX_NODEID_ANY,
                       FLUX_SUBPROCESS_FLAGS_SIGN,
                       cmd,
                       &kill_ops,
                       tap_logger,
                       NULL);
    if (!p)
        BAIL_OUT ("flux_rexec_ex failed");
    if (flux_subprocess_aux_set (p, "ctx", ctx, NULL) < 0)
        BAIL_OUT ("flux_subprocess_aux_set failed");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        BAIL_OUT ("flux_reactor_run failed");
    flux_cmd_destroy (cmd);
    return p;
}

/* An unsigned kill to a sign-required server must be rejected with EPERM. */
static void test_unsigned_kill_rejected (flux_t *h)
{
    struct kill_ctx ctx = { .h = h };
    flux_subprocess_t *p;
    flux_future_t *f;
    const char *errmsg;

    p = start_sleep (h, &ctx);
    if (!ctx.started)
        BAIL_OUT ("sleep process did not start");

    f = subprocess_kill (h, SERVER_NAME, FLUX_NODEID_ANY, ctx.pid, SIGTERM, false);
    if (!f)
        BAIL_OUT ("subprocess_kill failed");
    ok (flux_future_get (f, NULL) < 0 && errno == EPERM,
        "unsigned kill to sign-required server gets EPERM");
    errmsg = flux_future_error_string (f);
    diag ("error: %s", errmsg ? errmsg : "(none)");
    like (errmsg, "request signature required",
          "error mentions request signature required");
    flux_future_destroy (f);

    /* Clean up: kill with signing so the reactor loop below can finish. */
    f = subprocess_kill (h, SERVER_NAME, FLUX_NODEID_ANY, ctx.pid, SIGTERM, true);
    if (!f || flux_future_get (f, NULL) < 0)
        BAIL_OUT ("signed cleanup kill failed");
    flux_future_destroy (f);

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        BAIL_OUT ("flux_reactor_run failed");
    flux_subprocess_destroy (p);
}

/* A signed kill to a sign-required server must succeed. */
static void test_signed_kill_succeeds (flux_t *h)
{
    struct kill_ctx ctx = { .h = h };
    flux_subprocess_t *p;
    flux_future_t *f;

    p = start_sleep (h, &ctx);
    if (!ctx.started)
        BAIL_OUT ("sleep process did not start");

    f = subprocess_kill (h, SERVER_NAME, FLUX_NODEID_ANY, ctx.pid, SIGTERM, true);
    if (!f)
        BAIL_OUT ("subprocess_kill failed");
    ok (flux_future_get (f, NULL) == 0,
        "signed kill to sign-required server succeeds");
    flux_future_destroy (f);

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        BAIL_OUT ("flux_reactor_run failed");
    ok (ctx.completed,
        "process completed after signed kill");
    flux_subprocess_destroy (p);
}

static bool sign_wrap_works (void)
{
    flux_security_t *sec = flux_security_create (0);
    const char *s;
    bool result = true;
    if (flux_security_configure (sec, NULL) < 0
        || !(s = flux_sign_wrap (sec, "foo", 3, NULL, 0)))
        result = false;
    flux_security_destroy (sec);
    return result;
}

int main (int argc, char *argv[])
{
    flux_t *h;

    if (!sign_wrap_works ()) {
        plan (0);
        diag ("skipping all tests due to non-function flux_sign_wrap()");
        done_testing ();
        return 0;
    }

    plan (NO_PLAN);

    h = rcmdsrv_create_secure (SERVER_NAME);

    diag ("test_unsigned_rejected");
    test_unsigned_rejected (h);

    diag ("test_signed_succeeds");
    test_signed_succeeds (h);

    diag ("test_wrong_uid_rejected");
    test_wrong_uid_rejected (h);

    diag ("test_invalid_sig_type");
    test_invalid_sig_type (h);

    diag ("test_malformed_payload");
    test_malformed_payload (h);

    diag ("test_tampered_signature");
    test_tampered_signature (h);

    diag ("test_topic_mismatch");
    test_topic_mismatch (h);

    diag ("test_missing_required_field");
    test_missing_required_field (h);

    diag ("test_wrong_field_type");
    test_wrong_field_type (h);

    diag ("test_cmd_wrong_type");
    test_cmd_wrong_type (h);

    diag ("test_unsigned_kill_rejected");
    test_unsigned_kill_rejected (h);

    diag ("test_signed_kill_succeeds");
    test_signed_kill_succeeds (h);

    test_server_stop (h);
    flux_close (h);

    done_testing ();
    return 0;
}

#else /* !HAVE_FLUX_SECURITY */

int main (int argc, char *argv[])
{
    plan (0);
    diag ("No flux-security, skipping all tests");
    done_testing ();
    return 0;
}

#endif /* HAVE_FLUX_SECURITY */

// vi: ts=4 sw=4 expandtab

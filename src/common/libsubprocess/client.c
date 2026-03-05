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
# include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <flux/core.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/sign.h>
#endif

#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"
#include "src/common/libioencode/ioencode.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"

#include "command_private.h"
#include "client.h"

struct rexec_io {
    json_t *obj;
    const char *stream;
    char *data;
    int len;
    bool eof;
};

struct rexec_response {
    const char *type;
    pid_t pid;
    int status;
    struct rexec_io io;
    json_t *channels;
};

struct rexec_ctx {
    json_t *cmd;
    int flags;
    struct rexec_response response;
    uint32_t matchtag;
    uint32_t rank;
    char *service_name;
    bool sign;
};

static void rexec_response_clear (struct rexec_response *resp)
{
    json_decref (resp->io.obj);
    free (resp->io.data);
    json_decref (resp->channels);
    resp->channels = NULL;

    memset (resp, 0, sizeof (*resp));

    resp->pid = -1;
}

static void rexec_ctx_destroy (struct rexec_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        rexec_response_clear (&ctx->response);
        json_decref (ctx->cmd);
        free (ctx->service_name);
        free (ctx);
        errno = saved_errno;
    }
}

static struct rexec_ctx *rexec_ctx_create (flux_cmd_t *cmd,
                                           const char *service_name,
                                           uint32_t rank,
                                           int flags)
{
    struct rexec_ctx *ctx;
    int valid_flags = SUBPROCESS_REXEC_STDOUT
        | SUBPROCESS_REXEC_STDERR
        | SUBPROCESS_REXEC_CHANNEL
        | SUBPROCESS_REXEC_WRITE_CREDIT;

    if ((flags & ~valid_flags)) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if (!(ctx->cmd = cmd_tojson (cmd))
        || !(ctx->service_name = strdup (service_name)))
        goto error;
    ctx->flags = flags;
    ctx->response.pid = -1;
    ctx->rank = rank;
    return ctx;
error:
    rexec_ctx_destroy (ctx);
    return NULL;
}

#if HAVE_FLUX_SECURITY
flux_security_t *subprocess_get_security (flux_t *h)
{
    flux_security_t *sec;

    if (!(sec = flux_aux_get (h, "flux::subprocess::sign"))) {
        if (!(sec = flux_security_create (0))
            || flux_security_configure (sec, NULL) < 0
            || flux_aux_set (h,
                             "flux::subprocess::sign",
                             sec,
                             (flux_free_f)flux_security_destroy) < 0) {
            flux_security_destroy (sec);
            return NULL;
        }
    }
    return sec;
}

/* Sign a JSON payload and send the request as {"signature": token}.
 * On success payload is modified with signature field.
 * Returns a new future on success, NULL on error.
 */
static flux_future_t *rpc_send_signed (flux_t *h,
                                       const char *topic,
                                       uint32_t rank,
                                       int flags,
                                       int signature_only,
                                       json_t *payload)
{
    flux_security_t *sec;
    char *s = NULL;
    const char *signature;
    flux_future_t *f = NULL;
    json_t *o = NULL;

    if (!(o = json_string (topic))
        || json_object_set_new (payload, "topic", o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        goto out;
    }
    if (!(s = json_dumps (payload, JSON_COMPACT))) {
        errno = ENOMEM;
        goto out;
    }
    if (!(sec = subprocess_get_security (h)))
        goto out;
    if (!(signature = flux_sign_wrap (sec, s, strlen (s), NULL, 0))) {
        errno = flux_security_last_errnum (sec);
        goto out;
    }
    if (signature_only)
        f = flux_rpc_pack (h,
                           topic,
                           rank,
                           flags,
                           "{s:s}",
                           "signature", signature);
    else {
        json_t *osig = NULL;
        (void) json_object_del (payload, "topic");
        if (!(osig = json_string (signature))
            || json_object_set_new (payload, "signature", osig) < 0) {
            json_decref (osig);
            errno = ENOMEM;
            goto out;
        }
        f = flux_rpc_pack (h, topic, rank, flags, "O", payload);
    }
out:
    if (!f) {
        /* On failure, delete keys that may have been added to payload.
         */
        (void) json_object_del (payload, "topic");
        (void) json_object_del (payload, "signature");
    }
    ERRNO_SAFE_WRAP (free, s);
    return f;
}

#endif /* HAVE_FLUX_SECURITY */

/* Wrapper for flux_rpc_pack() which sends signed request if flux-security
 * is available and sign == true.
 */
static flux_future_t *rpc_pack_signed (flux_t *h,
                                       int sign,
                                       const char *topic,
                                       uint32_t rank,
                                       int flags,
                                       int signature_only,
                                       const char *fmt,
                                       ...)
{
    va_list ap;
    flux_future_t *f;

#if HAVE_FLUX_SECURITY
    if (sign) {
        json_t *payload;

        va_start (ap, fmt);
        payload = json_vpack_ex (NULL, 0, fmt, ap);
        va_end (ap);
        if (!payload) {
            errno = EINVAL;
            return NULL;
        }
        f = rpc_send_signed (h, topic, rank, flags, signature_only, payload);
        ERRNO_SAFE_WRAP (json_decref, payload);
        return f;
    }
#else /* !HAVE_FLUX_SECURITY */
    if (sign) {
        errno = ENOTSUP;
        return NULL;
    }
#endif /* HAVE_FLUX_SECURITY */

    va_start (ap, fmt);
    f = flux_rpc_vpack (h, topic, rank, flags, fmt, ap);
    va_end (ap);

    return f;
}


flux_future_t *subprocess_rexec (flux_t *h,
                                 const char *service_name,
                                 uint32_t rank,
                                 flux_cmd_t *cmd,
                                 int flags,
                                 int local_flags)
{
    flux_future_t *f = NULL;
    struct rexec_ctx *ctx;
    char *topic;

    if (!h || !cmd || !service_name) {
        errno = EINVAL;
        return NULL;
    }
    if (asprintf (&topic, "%s.exec", service_name) < 0)
        return NULL;
    if (!(ctx = rexec_ctx_create (cmd, service_name, rank, flags)))
        goto error;
#if HAVE_FLUX_SECURITY
    if (local_flags & FLUX_SUBPROCESS_FLAGS_SIGN) {
        local_flags &= ~FLUX_SUBPROCESS_FLAGS_SIGN;
        ctx->sign = true;
    }
#endif
    f = rpc_pack_signed (h,
                         ctx->sign,
                         topic,
                         rank,
                         FLUX_RPC_STREAMING,
                         false,
                         "{s:O s:i s:i}",
                         "cmd", ctx->cmd,
                         "flags", ctx->flags,
                         "local_flags", local_flags);
    if (!f
        || flux_future_aux_set (f,
                                "flux::rexec",
                                ctx,
                                (flux_free_f)rexec_ctx_destroy) < 0) {
        rexec_ctx_destroy (ctx);
        goto error;
    }
    ctx->matchtag = flux_rpc_get_matchtag (f);
    free (topic);
    return f;
error:
    ERRNO_SAFE_WRAP (free, topic);
    flux_future_destroy (f);
    return NULL;
}

flux_future_t *subprocess_rexec_bg (flux_t *h,
                                    const char *service_name,
                                    uint32_t rank,
                                    const flux_cmd_t *cmd,
                                    int local_flags)
{
    flux_future_t *f = NULL;
    json_t *ocmd = NULL;
    char *topic = NULL;
    int flags = 0;

    bool sign = false;

    /* FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF is not allowed with background
     * execution, raise error if set:
     */
    if (local_flags & FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF) {
        errno = EINVAL;
        return NULL;
    }
    /* Move waitable flag from local_flags to flags
     */
    if (local_flags & FLUX_SUBPROCESS_FLAGS_WAITABLE) {
        local_flags &= ~FLUX_SUBPROCESS_FLAGS_WAITABLE;
        flags |= SUBPROCESS_REXEC_WAITABLE;
    }
#if HAVE_FLUX_SECURITY
    if (local_flags & FLUX_SUBPROCESS_FLAGS_SIGN) {
        local_flags &= ~FLUX_SUBPROCESS_FLAGS_SIGN;
        sign = true;
    }
#endif

    if (service_name == NULL)
        service_name = "rexec";

    if (asprintf (&topic, "%s.exec", service_name) < 0
        || !(ocmd = cmd_tojson (cmd)))
        goto out;

    f = rpc_pack_signed (h,
                         sign,
                         topic,
                         rank,
                         0,
                         false,
                         "{s:O s:i s:i}",
                         "cmd", ocmd,
                         "flags", flags,
                         "local_flags", local_flags);
out:
    ERRNO_SAFE_WRAP (free, topic);
    ERRNO_SAFE_WRAP (json_decref, ocmd);
    return f;
}

int subprocess_rexec_get (flux_future_t *f)
{
    struct rexec_ctx *ctx;

    if (!(ctx = flux_future_aux_get (f, "flux::rexec"))) {
        errno = EINVAL;
        return -1;
    }
    rexec_response_clear (&ctx->response);
    if (flux_rpc_get_unpack (f,
                             "{s:s s?i s?i s?O s?O}",
                             "type", &ctx->response.type,
                             "pid", &ctx->response.pid,
                             "status", &ctx->response.status,
                             "io", &ctx->response.io.obj,
                             "channels", &ctx->response.channels) < 0)
        return -1;
    if (streq (ctx->response.type, "output")) {
        if (iodecode (ctx->response.io.obj,
                      &ctx->response.io.stream,
                      NULL,
                      &ctx->response.io.data,
                      &ctx->response.io.len,
                      &ctx->response.io.eof) < 0)
            return -1;
    }
    else if (streq (ctx->response.type, "add-credit")) {
        const char *key;
        json_t *value;

        if (!ctx->response.channels
            || !json_is_object (ctx->response.channels)) {
            errno = EPROTO;
            return -1;
        }
        json_object_foreach (ctx->response.channels, key, value) {
            if (!json_is_integer (value)) {
                errno = EPROTO;
                return -1;
            }
        }
    }
    else if (!streq (ctx->response.type, "started")
        && !streq (ctx->response.type, "stopped")
        && !streq (ctx->response.type, "finished")) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

bool subprocess_rexec_is_started (flux_future_t *f, pid_t *pid)
{
    struct rexec_ctx *ctx;
    if ((ctx = flux_future_aux_get (f, "flux::rexec"))
        && ctx->response.type != NULL
        && streq (ctx->response.type, "started")) {
        if (pid)
            *pid = ctx->response.pid;
        return true;
    }
    return false;
}

bool subprocess_rexec_is_stopped (flux_future_t *f)
{
    struct rexec_ctx *ctx;
    if ((ctx = flux_future_aux_get (f, "flux::rexec"))
        && ctx->response.type != NULL
        && streq (ctx->response.type, "stopped"))
        return true;
    return false;
}

bool subprocess_rexec_is_finished (flux_future_t *f, int *status)
{
    struct rexec_ctx *ctx;
    if ((ctx = flux_future_aux_get (f, "flux::rexec"))
        && ctx->response.type != NULL
        && streq (ctx->response.type, "finished")) {
        if (status)
            *status = ctx->response.status;
        return true;
    }
    return false;
}

bool subprocess_rexec_is_output (flux_future_t *f,
                                 const char **stream,
                                 const char **data,
                                 int *len,
                                 bool *eof)
{
    struct rexec_ctx *ctx;
    if ((ctx = flux_future_aux_get (f, "flux::rexec"))
        && ctx->response.type != NULL
        && streq (ctx->response.type, "output")) {
        if (stream)
            *stream = ctx->response.io.stream;
        if (data)
            *data = ctx->response.io.data;
        if (len)
            *len = ctx->response.io.len;
        if (eof)
            *eof = ctx->response.io.eof;
        return true;
    }
    return false;
}

bool subprocess_rexec_is_add_credit (flux_future_t *f, json_t **channels)
{
    struct rexec_ctx *ctx;
    if ((ctx = flux_future_aux_get (f, "flux::rexec"))
        && ctx->response.type != NULL
        && streq (ctx->response.type, "add-credit")) {
        if (channels)
            (*channels) = ctx->response.channels;
        return true;
    }
    return false;
}

int subprocess_write (flux_future_t *f_exec,
                      const char *stream,
                      const char *data,
                      int len,
                      bool eof)
{
    struct rexec_ctx *ctx = flux_future_aux_get (f_exec, "flux::rexec");
    flux_t *h = flux_future_get_flux (f_exec);
    flux_future_t *f = NULL;
    json_t *io = NULL;
    char *topic;
    int rc = -1;

    if (!stream || !ctx) {
        errno = EINVAL;
        return -1;
    }
    if (asprintf (&topic, "%s.write", ctx->service_name) < 0)
        return -1;
    if (!(io = ioencode (stream, "0", data, len, eof))
        || !(f = rpc_pack_signed (h,
                                  ctx->sign,
                                  topic,
                                  ctx->rank,
                                  FLUX_RPC_NORESPONSE,
                                  true,
                                  "{s:i s:O}",
                                  "matchtag", ctx->matchtag,
                                  "io", io)))
        goto out;
    rc = 0;
out:
    flux_future_destroy (f);
    ERRNO_SAFE_WRAP (json_decref, io);
    ERRNO_SAFE_WRAP (free, topic);
    return rc;
}

flux_future_t *subprocess_kill (flux_t *h,
                                const char *service_name,
                                uint32_t rank,
                                pid_t pid,
                                int signum,
                                bool sign)
{
    flux_future_t *f;
    char *topic;

    if (!h || !service_name) {
        errno = EINVAL;
        return NULL;
    }
    if (asprintf (&topic, "%s.kill", service_name) < 0)
        return NULL;
    if (!(f = rpc_pack_signed (h,
                               sign,
                               topic,
                               rank,
                               0,
                               false,
                               "{s:i s:i}",
                               "pid", pid,
                               "signum", signum))) {
        ERRNO_SAFE_WRAP (free, topic);
        return NULL;
    }
    free (topic);
    return f;
}

// vi: ts=4 sw=4 expandtab

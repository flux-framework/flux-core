/* Conformance fixture for .clang-format + scripts/clang-format-flux.
 * Every construct here follows RFC 7 (or, where RFC 7 is silent, the
 * prevailing flux-core convention).  Formatting must be a no-op.
 */
#include <stdio.h>

#define FOO_MAGIC       0x1234
#define FOO_LONGER_NAME 42

struct foo {
    int *ptr;               // RFC 7 section 8
    const char *name;       // trailing comments are left alone
};

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-manager.list", list_handle_request, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static struct optparse_option opts[] = {
    {
        .name = "verbose",
        .key = 'v',
        .has_arg = 0,
        .usage = "List keys on stderr as they are archived",
    },
    OPTPARSE_TABLE_END
};

/* RFC 7 section 9 */
flux_future_t *flux_rpc (flux_t *h,
                         const char *topic,
                         const char *s,
                         uint32_t nodeid,
                         int flags);

static int example (flux_t *h, const flux_msg_t *msg, struct foo *out)
{
    flux_future_t *f;
    const char *topic;
    json_t *o;
    const char *key;
    json_t *value;
    char *argv[] = { "this", "is", "a", "test" };

    f = flux_rpc (h,
                  "service.do-something",
                  NULL,
                  FLUX_NODEID_ANY,
                  FLUX_RPC_RESPONSE);

    /* RFC 7 section 10 */
    if (out->type == MY_EXPECTED_TYPE
        && out->bytes > LOW_END_RANGE
        && out->bytes < HIGH_END_RANGE) {
        out->ptr = NULL;
    }
    if (flux_event_subscribe (h, "event.foo") < 0
        || flux_event_subscribe (h, "event.bar") < 0) {
        goto error;
    }

    /* jansson pack/unpack: key/value pairs grouped by hand */
    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:i}",
                             "topic", &topic,
                             "flags", &out->flags) < 0)
        goto error;

    /* comparison stays attached after a multi-line call */
    if (conf_register_callback (out->conf,
                                &out->error,
                                private_mode_update,
                                out) < 0) {
        goto error;
    }

    /* args would fit on one line, but not with the comparison attached */
    if (flux_msg_handler_addvec (boot->ctx->h,
                                 htab,
                                 boot,
                                 &boot->handlers) < 0) {
        goto error;
    }
    if (snprintf (uri, sizeof (uri), "ssh://%s%s", hostname, path)
        >= sizeof (uri)) {
        goto error;
    }

    json_object_foreach (o, key, value) {
        if (!json_is_string (value))
            continue;
    }

    switch (out->type) {
        case 1:
            break;
        default:
            break;
    }
    if (out->bytes == 0) {
        flux_log (h,
                  LOG_ERR,
                  "this is a long log message that must not be split by the formatter");
    }
    else if (out->bytes > 1) {
        return 1;
    }
    else {
        return 2;
    }
    return 0;
error:
    flux_future_destroy (f);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

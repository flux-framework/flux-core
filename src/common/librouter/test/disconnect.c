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
#include "ccan/str/str.h"
#include "src/common/librouter/disconnect.h"

/* Used for topic() and hashkeys() subtests */
struct stab {
    const char *topic;
    const char *out;
    uint32_t nodeid;
    uint8_t flags;
};

flux_msg_t *gen_request (const char *topic, uint32_t nodeid, uint8_t flags)
{
    uint8_t tmp;
    flux_msg_t *msg;

    if (!(msg = flux_request_encode (topic, NULL)))
        return NULL;
    if (flux_msg_get_flags (msg, &tmp) < 0)
        goto error;
    tmp |= flags;
    if (flux_msg_set_flags (msg, tmp) < 0)
        goto error;
    if (flux_msg_set_nodeid (msg, nodeid) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

struct stab topics[] = {
    { .topic = "foo",           .out = "disconnect" },
    { .topic = "foo.bar",       .out = "foo.disconnect" },
    { .topic = "foo.bar.baz",   .out = "foo.bar.disconnect" },
    { NULL, NULL, 0, 0 },
};

void topic (void)
{
    char buf[256];
    int i;

    for (i = 0; topics[i].topic != NULL; i++) {
        ok (disconnect_topic (topics[i].topic, buf, sizeof (buf)) >= 0
            && streq (buf, topics[i].out),
            "topic: %s => %s", topics[i].topic, topics[i].out);
    }

    errno = 0;
    ok (disconnect_topic ("foo", buf, 2) < 0 && errno == EINVAL,
        "topic: foo len=2 fails with EINVAL");

    errno = 0;
    ok (disconnect_topic (NULL, buf, sizeof (buf)) < 0 && errno == EINVAL,
        "topic: NULL fails with EINVAL");

    errno = 0;
    ok (disconnect_topic ("foo", NULL, sizeof (buf)) < 0 && errno == EINVAL,
        "topic: foo buf=NULL fails with EINVAL");
}

struct stab hashkeys[] = {
    {   .topic = "foo",
        .nodeid = 1,
        .flags = FLUX_MSGFLAG_UPSTREAM,
        .out = "disconnect:1:16",
    },
    {   .topic = "foo.bar",
        .nodeid = 1,
        .flags = 0,
        .out = "foo.disconnect:1:0",
    },
    {   .topic = "foo.bar",
        .nodeid = FLUX_NODEID_ANY,
        .flags = FLUX_MSGFLAG_STREAMING, // should be ignored,
        .out = "foo.disconnect:4294967295:0",
    },
    { NULL, NULL, 0, 0 },
};

void hashkey (void)
{
    char buf[256];
    int i;
    flux_msg_t *msg;

    for (i = 0; hashkeys[i].topic != NULL; i++) {
        if (!(msg = gen_request (hashkeys[i].topic,
                                 hashkeys[i].nodeid,
                                 hashkeys[i].flags)))
            BAIL_OUT ("gen_request failed");

        ok (disconnect_hashkey (msg, buf, sizeof (buf)) >= 0
            && streq (buf, hashkeys[i].out),
            "hashkey: %s,%u,%u => %s",
            hashkeys[i].topic,
            (unsigned int)hashkeys[i].nodeid,
            (unsigned int)hashkeys[i].flags,
            hashkeys[i].out);

        diag ("%s", buf);

        flux_msg_destroy (msg);
    }

    if (!(msg = gen_request ("foo", 0, 0)))
        BAIL_OUT ("gen_request failed");

    /* choose buffer size so that topic fails in first test,
     * and topic succeeds but remaining fields cannot be appended in second.
     * ("foo" needs 4 bytes, "foo:0:0" needs 8).
     */
    errno = 0;
    ok (disconnect_hashkey (msg, buf, 7) < 0 && errno == EINVAL,
        "hashkey: foo,0,0 len=7 fails with EINVAL");
    errno = 0;
    ok (disconnect_hashkey (msg, buf, 2) < 0 && errno == EINVAL,
        "hashkey: foo,0,0 len=2 fails with EINVAL");

    errno = 0;
    ok (disconnect_hashkey (NULL, buf, sizeof (buf)) < 0 && errno == EINVAL,
        "hashkey: NULL fails with EINVAL");

    errno = 0;
    ok (disconnect_hashkey (msg, NULL, sizeof (buf)) < 0 && errno == EINVAL,
        "hashkey: foo,0,0 buf=NULL fails with EINVAL");

    flux_msg_destroy (msg);
}

void count_cb (const flux_msg_t *msg, void *arg)
{
    int *count = arg;
    (*count)++;
}

void basic (void)
{
    struct disconnect *dcon;
    int count = 0;
    flux_msg_t *msg;

    dcon = disconnect_create (count_cb, &count);
    ok (dcon != NULL,
        "disconnect_create works");

    if (!(msg = gen_request ("foo.bar", 0, 0)))
        BAIL_OUT ("gen_request failed");
    ok (disconnect_arm (dcon, msg) == 0,
        "disconnect_arm works on foo.bar");
    ok (disconnect_arm (dcon, msg) == 0,
        "disconnect_arm works on foo.bar (again)");
    flux_msg_destroy (msg);

    if (!(msg = gen_request ("foo.baz", 0, 0)))
        BAIL_OUT ("gen_request failed");
    ok (disconnect_arm (dcon, msg) == 0,
        "disconnect_arm works on foo.baz");
    flux_msg_destroy (msg);

    if (!(msg = gen_request ("meep.oops", 0, 0)))
        BAIL_OUT ("gen_request failed");
    ok (disconnect_arm (dcon, msg) == 0,
        "disconnect_arm works on meep.oops");
    flux_msg_destroy (msg);

    disconnect_destroy (dcon);
    ok (count == 2,
        "callback invoked 2 times");
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    topic ();
    hashkey ();
    basic ();

    done_testing ();

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */

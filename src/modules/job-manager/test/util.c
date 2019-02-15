/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <stdbool.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_txn_private.h"

#include "src/modules/job-manager/job.h"
#include "src/modules/job-manager/util.h"

struct test_input {
    flux_jobid_t id;
    bool active;
    const char *key;
    const char *expected;
};

struct test_input intab[] = {
    { 1, true, NULL,            "job.active.0000.0000.0000.0001" },
    { 1, false, NULL,           "job.inactive.0000.0000.0000.0001" },
    { 2, true, "foo",           "job.active.0000.0000.0000.0002.foo" },
    { 2, false, "foo",          "job.inactive.0000.0000.0000.0002.foo" },
    { 3, true, "a.b.c",         "job.active.0000.0000.0000.0003.a.b.c" },
    { 0xdeadbeef, true, NULL,   "job.active.0000.0000.dead.beef" },

    /* expected failure: overflow */
    { 4, true, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", NULL },

    { 0, false, NULL, NULL },
};
bool is_intab_end (struct test_input *try)
{
    if (try->id == 0 && try->active == false && !try->key && !try->expected)
        return true;
    return false;
}

void check_one_jobkey (struct test_input *try)
{
    char path[64];
    struct job *job;
    int len;
    bool valid = false;

    if (!(job = job_create (try->id, 0, 0, 0, 0)))
        BAIL_OUT ("job_create failed");

    memset (path, 0, sizeof (path));
    len = util_jobkey (path, sizeof (path), try->active, job, try->key);

    if (try->expected) {
        if (len >= 0 && len == strlen (try->expected)
                     && !strcmp (path, try->expected))
            valid = true;
    }
    else { // expected failure
        if (len < 0)
            valid = true;
    }
    ok (valid == true,
        "util_jobkey id=%llu active=%s key=%s %s",
        (unsigned long long)try->id,
        try->active ? "true" : "false",
        try->key ? try->key : "NULL",
        try->expected ? "works" : "fails");

    if (!valid)
        diag ("jobkey: %s", path);

    job_decref (job);
}

void check_jobkey (void)
{
    int i;
    for (i = 0; !is_intab_end (&intab[i]); i++)
        check_one_jobkey (&intab[i]);
}

char *decode_value (flux_kvs_txn_t *txn, int index, const char **key)
{
    json_t *txn_entry;
    const char *txn_key;
    int txn_flags;
    json_t *txn_dirent;
    char *data = "";
    int len = -1;

    if (txn_get_op (txn, index, &txn_entry) < 0)
        return NULL;
    if (txn_decode_op (txn_entry, &txn_key, &txn_flags, &txn_dirent) < 0)
        return NULL;
    if (treeobj_decode_val (txn_dirent, (void **)&data, &len) < 0)
        return NULL;
    if (len == 0)
        return NULL;
    *key = txn_key;
    return data; // includes terminating \0 not included in len
}

void check_eventlog_append (void)
{
    flux_kvs_txn_t *txn;
    struct job *job;
    char *event;
    char name[FLUX_KVS_MAX_EVENT_NAME + 1];
    char context[FLUX_KVS_MAX_EVENT_CONTEXT + 1];
    const char *key;

    if (!(txn = flux_kvs_txn_create ()))
        BAIL_OUT ("flux_kvs_txn_create failed");
    if (!(job = job_create (3, 0, 0, 0, 0)))
        BAIL_OUT ("job_create failed");

    /* verify with context */
    ok (util_eventlog_append (txn, job, "foo", "%s", "testing") == 0,
        "util_eventlog_append id=3 name=foo ctx=testing works");
    event = decode_value (txn, 0, &key);
    ok (event && !strcmp (key, "job.active.0000.0000.0000.0003.eventlog"),
        "event appended to txn has expected key");
    diag ("%s", key);
    ok (event && flux_kvs_event_decode (event, NULL, name, sizeof (name),
                                        context, sizeof (context)) == 0
        && !strcmp (name, "foo") && !strcmp (context, "testing"),
        "event appended to txn matches input");
    free (event);

    /* verify event without context */
    ok (util_eventlog_append (txn, job, "foo", "") == 0,
        "util_eventlog_append id=3 name=foo ctx=\"\" works");
    event = decode_value (txn, 1, &key);
    ok (event && !strcmp (key, "job.active.0000.0000.0000.0003.eventlog"),
        "event appended to txn has expected key");
    ok (event && flux_kvs_event_decode (event, NULL, name, sizeof (name),
                                        context, sizeof (context)) == 0
        && !strcmp (name, "foo") && !strcmp (context, ""),
        "event appended to txn matches input");
    free (event);


    job_decref (job);
    flux_kvs_txn_destroy (txn);
}

void check_attr_set (void)
{
    flux_kvs_txn_t *txn;
    struct job *job;
    char *value;
    const char *key;

    if (!(txn = flux_kvs_txn_create ()))
        BAIL_OUT ("flux_kvs_txn_create failed");
    if (!(job = job_create (3, 0, 0, 0, 0)))
        BAIL_OUT ("job_create failed");

    ok (util_attr_pack (txn, job, "a", "i", 42) == 0,
        "util_attr_pack id=3 i=42 works");
    value = decode_value (txn, 0, &key);
    ok (value && !strcmp (key, "job.active.0000.0000.0000.0003.a"),
        "job attr in txn has expected key");
    ok (value && !strcmp (value, "42"),
        "job attr in txn has expected value");
    free (value);

    job_decref (job);
    flux_kvs_txn_destroy (txn);
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);

    check_jobkey ();
    check_eventlog_append ();
    check_attr_set ();

    done_testing ();

    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */

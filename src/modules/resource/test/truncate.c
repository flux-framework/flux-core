/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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
#include <errno.h>
#include <string.h>

#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libeventlog/eventlog.h"

#include "src/modules/resource/truncate.h"

static char *drain_idset (json_t *drain)
{
    char *result = NULL;
    const char *key;
    json_t *val;
    struct idset *ids = idset_create (0, IDSET_FLAG_AUTOGROW);
    if (!ids)
        return NULL;

    json_object_foreach (drain, key, val) {
        if (idset_decode_add (ids, key, -1, NULL) < 0)
            goto out;
    }
    result = idset_encode (ids, IDSET_FLAG_RANGE);
out:
    idset_destroy (ids);
    return result;
}

static void check_truncate (struct truncate_info *ti,
                            double expected_timestamp,
                            const char *expected_online,
                            const char *expected_torpid,
                            const char *expected_drained,
                            const char *expected_method)
{
    double timestamp;
    const char *online;
    const char *torpid;
    const char *method = NULL;
    const char *ranks = NULL;
    const char *nodelist = NULL;
    char *drained = NULL;
    json_t *drain;
    json_t *o = truncate_info_event (ti);

    if (!o)
        BAIL_OUT ("truncate_info_event failed!");

    if (json_unpack (o,
                     "{s:f s:{s:s s:s s:o s?s s?s s?s}}",
                     "timestamp", &timestamp,
                     "context",
                      "online", &online,
                      "torpid", &torpid,
                      "drain", &drain,
                      "ranks", &ranks,
                      "nodelist", &nodelist,
                      "discovery-method", &method) < 0)
        BAIL_OUT ("json_unpack of truncate event failed");

    is (online, expected_online,
        "got expected online \"%s\"=\"%s\"",
        online,
        expected_online);
    is (torpid, expected_torpid,
        "got expected torpid \"%s\"=\"%s\"",
        torpid,
        expected_torpid);
    if (!(drained = drain_idset (drain)))
        BAIL_OUT ("failed to get drained ranks from drain object");
    is (drained, expected_drained,
        "got expected drained ranks \"%s\"=\"%s\"",
        drained,
        expected_drained);
    if (expected_method)
        is (method, expected_method,
            "got expected discovery-method \"%s\"=\"%s\"",
            method,
            expected_method);
    if (expected_timestamp > 0.)
        ok (timestamp == expected_timestamp,
            "got expected timestamp");

    json_decref (o);
    free (drained);
}

static void test_empty ()
{
    struct truncate_info *ti = truncate_info_create ();
    if (!ti)
        BAIL_OUT ("truncate_info_create()/event() failed!");
    ok (true, "created empty truncate object");
    check_truncate (ti, -1., "", "", "", NULL);
    truncate_info_destroy (ti);
}

static void test_invalid ()
{
    json_t *event;
    struct truncate_info *ti = truncate_info_create ();

    ok (truncate_info_event (NULL) == NULL && errno == EINVAL,
        "truncate_info_event (NULL) returns EINVAL");

    ok (truncate_info_update (NULL, NULL) < 0 && errno == EINVAL,
        "truncate_info_update (NULL, NULL) returns EINVAL");
    ok (truncate_info_update (ti, NULL) < 0 && errno == EINVAL,
        "truncate_info_update (ti, NULL) returns EINVAL");

    /* create bad event (empty JSON object)
     */
    if (!(event = json_object ()))
        BAIL_OUT ("failed to create bad event for testing");
    ok (truncate_info_update (ti, event) < 0 && errno == EINVAL,
        "truncate_info_update with bad event returns EINVAL");
    json_decref (event);

    /* create good event, bad event name
     */
    if (!(event = eventlog_entry_create (0., "foo", NULL)))
        BAIL_OUT ("eventlog_entry_create (foo)");
    ok (truncate_info_update (ti, event) < 0 && errno == ENOENT,
        "truncate_info_update with unknown event name returns ENOENT");
    json_decref (event);

    truncate_info_destroy (ti);
}

struct test_entry {
    const char *name;
    const char *context;
    const char *online;
    const char *torpid;
    const char *drained;
    const char *method;
};

/* Sequence of basic resource events and expected online, torpid, etc.
 * from the truncate object.
 */
struct test_entry tests[] = {
    { .name = "restart",
      .context= "{\"ranks\":\"0-3\","
                 "\"nodelist\":\"foo[0-3]\","
                 "\"online\":\"\",\"torpid\":\"\"}",
      .online = "",
      .torpid = "",
      .drained = "",
      .method = NULL
    },
    { .name = "online",
      .context= "{\"idset\":\"0\"}",
      .online = "0",
      .torpid = "",
      .drained = "",
      .method = NULL
    },
    { .name = "online",
      .context= "{\"idset\":\"1-3\"}",
      .online = "0-3",
      .torpid = "",
      .drained = "",
      .method = NULL
    },
    { .name = "resource-define",
      .context= "{\"method\":\"dynamic-discovery\"}",
      .online = "0-3",
      .torpid = "",
      .drained = "",
      .method = "dynamic-discovery",
    },
    { .name = "torpid",
      .context= "{\"idset\":\"3\"}",
      .online = "0-3",
      .torpid = "3",
      .drained = "",
      .method = "dynamic-discovery",
    },
    { .name = "lively",
      .context= "{\"idset\":\"3\"}",
      .online = "0-3",
      .torpid = "",
      .drained = "",
      .method = "dynamic-discovery",
    },
    { .name = "offline",
      .context= "{\"idset\":\"3\"}",
      .online = "0-2",
      .torpid = "",
      .drained = "",
      .method = "dynamic-discovery",
    },
    { .name = "drain",
      .context= "{\"idset\":\"1\","
                 "\"nodelist\":\"foo1\","
                 "\"overwrite\":0}",
      .online = "0-2",
      .torpid = "",
      .drained = "1",
      .method = "dynamic-discovery",
    },
    /* Flux allows drain event with overwrite=0 if there
     * is no reason.
     */
    { .name = "drain",
      .context= "{\"idset\":\"1\","
                 "\"nodelist\":\"foo1\","
                 "\"overwrite\":0}",
      .online = "0-2",
      .torpid = "",
      .drained = "1",
      .method = "dynamic-discovery",
    },
    { .name = "undrain",
      .context= "{\"idset\":\"1\"}",
      .online = "0-2",
      .torpid = "",
      .drained = "",
      .method = "dynamic-discovery",
    },
    { .name = "drain",
      .context= "{\"idset\":\"0\","
                 "\"nodelist\":\"foo0\","
                 "\"reason\":\"test\","
                 "\"overwrite\":0}",
      .online = "0-2",
      .torpid = "",
      .drained = "0",
      .method = "dynamic-discovery",
    },
    { .name = "drain",
      .context= "{\"idset\":\"1\","
                 "\"nodelist\":\"foo0\","
                 "\"reason\":\"test\","
                 "\"overwrite\":0}",
      .online = "0-2",
      .torpid = "",
      .drained = "0-1",
      .method = "dynamic-discovery",
    },
    { .name = "undrain",
      .context= "{\"idset\":\"0\"}",
      .online = "0-2",
      .torpid = "",
      .drained = "1",
      .method = "dynamic-discovery",
    },
    { NULL, NULL, NULL, NULL, NULL, NULL },
};

static void test_simple ()
{
    struct test_entry *t = tests;
    json_t *event;
    struct truncate_info *ti = truncate_info_create ();

    if (!ti)
        BAIL_OUT ("Failed to create truncate info object");

    while (t->name) {
        if (!(event = eventlog_entry_create (0., t->name, t->context)))
            BAIL_OUT ("failed to create %s event context", t->name);
        ok (truncate_info_update (ti, event) == 0,
            "truncate_info_update '%s' worked",
            t->name);
        json_decref (event);
        check_truncate (ti, -1., t->online, t->torpid, t->drained, t->method);
        ++t;
    }

    truncate_info_destroy (ti);
}

static void test_from_truncate ()
{
    json_t *event;
    const char *context =
        "{\"online\":\"0-3\","
         "\"torpid\":\"\","
         "\"ranks\":\"0-3\","
         "\"nodelist\":\"foo[0-3]\","
         "\"drain\":{\"0-1\":{\"reason\":\"foo\",\"timestamp\":1.0}},"
         "\"discovery-method\":\"dynamic-discovery\"}";

    struct truncate_info *ti = truncate_info_create ();
    if (!ti)
        BAIL_OUT ("truncate_info_create failed");

    if (!(event = eventlog_entry_create (0., "truncate", context)))
        BAIL_OUT ("failed to create truncate event: %s", strerror (errno));

    ok (truncate_info_update (ti, event) == 0,
        "truncate_info_update 'truncate' worked");

    check_truncate (ti, -1., "0-3", "", "0-1", "dynamic-discovery");

    json_decref (event);
    truncate_info_destroy (ti);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);
    test_empty ();
    test_invalid ();
    test_simple ();
    test_from_truncate ();
    done_testing ();
    return (0);
}

/*
 * vi:ts=4 sw=4 expandtab
 */

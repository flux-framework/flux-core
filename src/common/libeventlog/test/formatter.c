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
#include "config.h"
#endif
#include <errno.h>
#include <string.h>
#include <time.h>

#include "src/common/libtap/tap.h"
#include "src/common/libeventlog/formatter.h"

struct test_entry {
    const char *input;
    const char *raw;
    const char *iso;
    const char *offset;
    const char *human;
};

/*  This input set was constructed from an real eventlog.
 *  Events must be kept in sequence so that offsets are calculated
 *  correctly
 */
struct test_entry tests[] = {
    { "{\"timestamp\":1699995759.5377746,\"name\":\"submit\",\"context\":{\"userid\":1001,\"urgency\":16,\"flags\":0,\"version\":1}}",
      "1699995759.537775 submit userid=1001 urgency=16 flags=0 version=1",
      "2023-11-14T21:02:39.537774Z submit userid=1001 urgency=16 flags=0 version=1",
      "0.000000 submit userid=1001 urgency=16 flags=0 version=1",
      "\x1b[1m\x1b[32m[Nov14 21:02]\x1b[0m \x1b[33msubmit\x1b[0m \x1b[34muserid\x1b[0m=\x1b[37m1001\x1b[0m \x1b[34murgency\x1b[0m=\x1b[37m16\x1b[0m \x1b[34mflags\x1b[0m=\x1b[37m0\x1b[0m \x1b[34mversion\x1b[0m=\x1b[37m1\x1b[0m",
    },
    {
      "{\"timestamp\":1699995759.5597851,\"name\":\"validate\"}",
      "1699995759.559785 validate",
      "2023-11-14T21:02:39.559785Z validate",
      "0.022011 validate",
      "\x1b[32m[  +0.022011]\x1b[0m \x1b[33mvalidate\x1b[0m",
    },
    {
      "{\"timestamp\":1699995759.5738351,\"name\":\"depend\"}",
      "1699995759.573835 depend",
      "2023-11-14T21:02:39.573835Z depend",
      "0.036061 depend",
      "\x1b[32m[  +0.036061]\x1b[0m \x1b[33mdepend\x1b[0m",
    },
    {
      "{\"timestamp\":1699995759.5739679,\"name\":\"priority\",\"context\":{\"priority\":66963}}",
      "1699995759.573968 priority priority=66963",
      "2023-11-14T21:02:39.573967Z priority priority=66963",
      "0.036193 priority priority=66963",
      "\x1b[32m[  +0.036193]\x1b[0m \x1b[33mpriority\x1b[0m \x1b[34mpriority\x1b[0m=\x1b[37m66963\x1b[0m",
    },
    {
      "{\"timestamp\":1699995759.6047542,\"name\":\"alloc\"}",
      "1699995759.604754 alloc",
      "2023-11-14T21:02:39.604754Z alloc",
      "0.066980 alloc",
      "\x1b[32m[  +0.066980]\x1b[0m \x1b[33malloc\x1b[0m",
    },
    {
      "{\"timestamp\":1699995759.6055193,\"name\":\"prolog-start\",\"context\":{\"description\":\"job-manager.prolog\"}}",
      "1699995759.605519 prolog-start description=\"job-manager.prolog\"",
      "2023-11-14T21:02:39.605519Z prolog-start description=\"job-manager.prolog\"",
      "0.067745 prolog-start description=\"job-manager.prolog\"",
      "\x1b[32m[  +0.067745]\x1b[0m \x1b[33mprolog-start\x1b[0m \x1b[34mdescription\x1b[0m=\x1b[35m\"job-manager.prolog\"\x1b[0m",
    },
    {
      "{\"timestamp\":1699995759.6055939,\"name\":\"prolog-start\",\"context\":{\"description\":\"cray-pals-port-distributor\"}}",
      "1699995759.605594 prolog-start description=\"cray-pals-port-distributor\"",
      "2023-11-14T21:02:39.605593Z prolog-start description=\"cray-pals-port-distributor\"",
      "0.067819 prolog-start description=\"cray-pals-port-distributor\"",
      "\x1b[32m[  +0.067819]\x1b[0m \x1b[33mprolog-start\x1b[0m \x1b[34mdescription\x1b[0m=\x1b[35m\"cray-pals-port-distributor\"\x1b[0m",
    },
    {
      "{\"timestamp\":1699995759.7634473,\"name\":\"prolog-finish\",\"context\":{\"description\":\"cray-pals-port-distributor\",\"status\":0}}",
      "1699995759.763447 prolog-finish description=\"cray-pals-port-distributor\" status=0",
      "2023-11-14T21:02:39.763447Z prolog-finish description=\"cray-pals-port-distributor\" status=0",
      "0.225673 prolog-finish description=\"cray-pals-port-distributor\" status=0",
      "\x1b[32m[  +0.225673]\x1b[0m \x1b[33mprolog-finish\x1b[0m \x1b[34mdescription\x1b[0m=\x1b[35m\"cray-pals-port-distributor\"\x1b[0m \x1b[34mstatus\x1b[0m=\x1b[37m0\x1b[0m",
    },
    {
      "{\"timestamp\":1699995760.3795953,\"name\":\"prolog-finish\",\"context\":{\"description\":\"job-manager.prolog\",\"status\":0}}",
      "1699995760.379595 prolog-finish description=\"job-manager.prolog\" status=0",
      "2023-11-14T21:02:40.379595Z prolog-finish description=\"job-manager.prolog\" status=0",
      "0.841821 prolog-finish description=\"job-manager.prolog\" status=0",
      "\x1b[32m[  +0.841821]\x1b[0m \x1b[33mprolog-finish\x1b[0m \x1b[34mdescription\x1b[0m=\x1b[35m\"job-manager.prolog\"\x1b[0m \x1b[34mstatus\x1b[0m=\x1b[37m0\x1b[0m",
    },
    {
      "{\"timestamp\":1699995760.3859105,\"name\":\"start\"}",
      "1699995760.385911 start",
      "2023-11-14T21:02:40.385910Z start",
      "0.848136 start",
      "\x1b[32m[  +0.848136]\x1b[0m \x1b[33mstart\x1b[0m",
    },
    {
      "{\"timestamp\":1699995760.7054179,\"name\":\"memo\",\"context\":{\"uri\":\"ssh://host/var/tmp/user/flux-0QZyMU/local-0\"}}",
      "1699995760.705418 memo uri=\"ssh://host/var/tmp/user/flux-0QZyMU/local-0\"",
      "2023-11-14T21:02:40.705417Z memo uri=\"ssh://host/var/tmp/user/flux-0QZyMU/local-0\"",
      "1.167643 memo uri=\"ssh://host/var/tmp/user/flux-0QZyMU/local-0\"",
      "\x1b[32m[  +1.167643]\x1b[0m \x1b[33mmemo\x1b[0m \x1b[34muri\x1b[0m=\x1b[35m\"ssh://host/var/tmp/user/flux-0QZyMU/local-0\"\x1b[0m",
    },
    {
      "{\"timestamp\":1700074161.0240808,\"name\":\"finish\",\"context\":{\"status\":0}}",
      "1700074161.024081 finish status=0",
      "2023-11-15T18:49:21.024080Z finish status=0",
      "78401.486306 finish status=0",
      "\x1b[1m\x1b[32m[Nov15 18:49]\x1b[0m \x1b[33mfinish\x1b[0m \x1b[34mstatus\x1b[0m=\x1b[37m0\x1b[0m",
    },
    {
      "{\"timestamp\":1700074161.0250554,\"name\":\"epilog-start\",\"context\":{\"description\":\"job-manager.epilog\"}}",
      "1700074161.025055 epilog-start description=\"job-manager.epilog\"",
      "2023-11-15T18:49:21.025055Z epilog-start description=\"job-manager.epilog\"",
      "78401.487281 epilog-start description=\"job-manager.epilog\"",
      "\x1b[32m[  +0.000975]\x1b[0m \x1b[33mepilog-start\x1b[0m \x1b[34mdescription\x1b[0m=\x1b[35m\"job-manager.epilog\"\x1b[0m",
    },
    {
      "{\"timestamp\":1700074161.1864166,\"name\":\"release\",\"context\":{\"ranks\":\"all\",\"final\":true}}",
      "1700074161.186417 release ranks=\"all\" final=true",
      "2023-11-15T18:49:21.186416Z release ranks=\"all\" final=true",
      "78401.648642 release ranks=\"all\" final=true",
      "\x1b[32m[  +0.162336]\x1b[0m \x1b[33mrelease\x1b[0m \x1b[34mranks\x1b[0m=\x1b[35m\"all\"\x1b[0m \x1b[34mfinal\x1b[0m=\x1b[35mtrue\x1b[0m",
    },
    {
      "{\"timestamp\":1700074445.1199436,\"name\":\"epilog-finish\",\"context\":{\"description\":\"job-manager.epilog\",\"status\":0}}",
      "1700074445.119944 epilog-finish description=\"job-manager.epilog\" status=0",
      "2023-11-15T18:54:05.119943Z epilog-finish description=\"job-manager.epilog\" status=0",
      "78685.582169 epilog-finish description=\"job-manager.epilog\" status=0",
      "\x1b[1m\x1b[32m[Nov15 18:54]\x1b[0m \x1b[33mepilog-finish\x1b[0m \x1b[34mdescription\x1b[0m=\x1b[35m\"job-manager.epilog\"\x1b[0m \x1b[34mstatus\x1b[0m=\x1b[37m0\x1b[0m",
    },
    {
      "{\"timestamp\":1700074445.1203697,\"name\":\"free\"}",
      "1700074445.120370 free",
      "2023-11-15T18:54:05.120369Z free",
      "78685.582595 free",
      "\x1b[32m[  +0.000426]\x1b[0m \x1b[33mfree\x1b[0m",
    },
    {
      "{\"timestamp\":1700074445.120451,\"name\":\"clean\"}",
      "1700074445.120451 clean",
      "2023-11-15T18:54:05.120450Z clean",
      "78685.582676 clean",
      "\x1b[32m[  +0.000507]\x1b[0m \x1b[33mclean\x1b[0m",
    },
    {0},
};

static void test_basic ()
{
    struct test_entry *test;
    flux_error_t error;
    struct eventlog_formatter *evf;
    if (!(evf = eventlog_formatter_create ()))
        BAIL_OUT ("failed to create eventlog formatter");

    eventlog_formatter_set_no_newline (evf);

    test = tests;
    while (test->input) {
        char *result;
        json_t *entry = json_loads (test->input, 0, NULL);
        if (!entry)
            BAIL_OUT ("failed to load JSON input '%s'", test->input);

        /* Disable color for tests */
        ok (eventlog_formatter_colors_init (evf, "never") == 0,
            "eventlog_formatter_colors_init (never)");

        /* 1. Test unformatted, should equal input
         */
        ok (eventlog_formatter_set_format (evf, "json") == 0,
            "eventlog_formatter_set_format(json)");
        ok ((result = eventlog_entry_dumps (evf, &error, entry)) != NULL,
            "eventlog_entry_dumps");
        diag (result);

        /* convert entry back to JSON string for comparison to avoid
         * idiosyncrasies of current jansson library (e.g. float precision)
         */
        char *s = json_dumps (entry, JSON_COMPACT);
        is (result, s,
            "json output is expected");
        free (result);
        free (s);
        /*  Need to reset unformatted flag for other tests:
         */
        ok (eventlog_formatter_set_format (evf, "text") == 0,
            "eventlog_formatter_set_format (text)");

        /*  2. Test raw timestamp
         */
        ok (eventlog_formatter_set_timestamp_format (evf, "raw") == 0,
            "eventlog_formatter_set_timestamp_format raw works");
        ok ((result = eventlog_entry_dumps (evf, &error, entry)) != NULL,
            "eventlog_entry_dumps");
        diag (result);
        is (result, test->raw,
            "raw timestamp output is expected");
        free (result);

        /* 3. Test ISO timestamp
         */
        ok (eventlog_formatter_set_timestamp_format (evf, "iso") == 0,
            "eventlog_formatter_set_timestamp_format iso works");
        ok ((result = eventlog_entry_dumps (evf, &error, entry)) != NULL,
            "eventlog_entry_dumps");
        diag (result);
        is (result, test->iso,
            "iso timestamp output is expected");
        free (result);

        /* 4. Test offset timestamp
         */
        ok (eventlog_formatter_set_timestamp_format (evf, "offset") == 0,
            "eventlog_formatter_set_timestamp_format offset works");
        ok ((result = eventlog_entry_dumps (evf, &error, entry)) != NULL,
            "eventlog_entry_dumps");
        diag (result);
        is (result, test->offset,
            "offset timestamp output is expected");
        free (result);

        /* 5. Test "reltime"/"human" timestamp with color
         */
        ok (eventlog_formatter_colors_init (evf, "always") == 0,
            "eventlog_formatter_colors_init (always)");
        ok (eventlog_formatter_set_timestamp_format (evf, "human") == 0,
            "eventlog_formatter_set_timestamp_format human works");
        ok ((result = eventlog_entry_dumps (evf, &error, entry)) != NULL,
            "eventlog_entry_dumps");
        diag (result);
        is (result, test->human,
            "human timestamp output is expected");
        free (result);

        json_decref (entry);
        test++;
    }

    eventlog_formatter_destroy (evf);
}

static void test_invalid ()
{
    struct eventlog_formatter *evf;
    json_t *good, *bad;
    flux_error_t error;

    if (!(evf = eventlog_formatter_create ()))
        BAIL_OUT ("failed to create eventlog formatter");

    lives_ok ({eventlog_formatter_destroy (NULL);});
    lives_ok ({eventlog_formatter_reset (NULL);});
    lives_ok ({eventlog_formatter_set_no_newline (NULL);});
    lives_ok ({eventlog_formatter_update_t0 (NULL, 0.);});

    ok (eventlog_formatter_set_timestamp_format (NULL, "") < 0
        && errno == EINVAL,
        "eventlog_formatter_set_timestamp_format (NULL) returns EINVAL");
    ok (eventlog_formatter_set_timestamp_format (evf, NULL) < 0
        && errno == EINVAL,
        "eventlog_formatter_set_timestamp_format (evf, NULL) returns EINVAL");
    ok (eventlog_formatter_set_timestamp_format (evf, "") < 0
        && errno == EINVAL,
        "eventlog_formatter_set_timestamp_format (evf, \"\") returns EINVAL");

    ok (eventlog_formatter_set_format (NULL, "text") < 0 && errno == EINVAL,
        "eventlog_formatter_set_format (NULL, \"text\") returns EINVAL");
    ok (eventlog_formatter_set_format (evf, "foo") < 0 && errno == EINVAL,
        "eventlog_formatter_set_format (evf, \"foo\") returns EINVAL");

    ok (eventlog_formatter_colors_init (NULL, "auto") < 0 && errno == EINVAL,
        "eventlog_formatter_colors_init (NULL, \"auto\") returns EINVAL");
    ok (eventlog_formatter_colors_init (evf, NULL) < 0 && errno == EINVAL,
        "eventlog_formatter_colors_init (evf, NULL) returns EINVAL");
    ok (eventlog_formatter_colors_init (evf, "foo") < 0 && errno == EINVAL,
        "eventlog_formatter_colors_init (evf, \"foo\") returns EINVAL");

    if (!(good = json_pack ("{s:f s:s s:{s:s}}",
                            "timestamp", 1699995759.0,
                            "name", "good",
                            "context",
                             "foo", "bar")))
        BAIL_OUT ("Failed to create good eventlog event");

    if (!(bad = json_pack ("{s:f s:s s:[s]}",
                            "timestamp", 1699995759.0,
                            "name", "bad",
                            "context",
                             "foo")))
        BAIL_OUT ("Failed to create bad eventlog event");

    /*  Check all results with default evf, then json formatted evf
     */
    for (int i = 0; i < 2; i++) {
        ok (eventlog_entry_dumpf (NULL, NULL, NULL, NULL) < 0
            && errno == EINVAL,
            "eventlog_entry_dumpf (NULL, ...) returns EINVAL");
        ok (eventlog_entry_dumpf (evf, NULL, NULL, NULL) < 0
            && errno == EINVAL,
            "eventlog_entry_dumpf (evf, NULL, ...) returns EINVAL");
        ok (eventlog_entry_dumpf (evf, stderr, NULL, NULL) < 0
            && errno == EINVAL,
            "eventlog_entry_dumpf (evf, stdout, NULL, ...) returns EINVAL");
        ok (eventlog_entry_dumpf (evf, NULL, &error, good) < 0
            && errno == EINVAL,
            "eventlog_entry_dumpf (evf, NULL, &error, event) returns EINVAL");
        ok (eventlog_entry_dumpf (evf, stderr, &error, bad) < 0
            && errno == EINVAL,
            "eventlog_entry_dumpf bad event returns EINVAL");
        is (error.text, "eventlog_entry_parse: Invalid argument");

        memset (&error, 0, sizeof (error));

        ok ((eventlog_entry_dumps (NULL, NULL, NULL) == NULL)
            && errno == EINVAL,
            "eventlog_entry_dumps (NULL, ...) returns EINVAL");
        ok ((eventlog_entry_dumps (evf, NULL, NULL) == NULL)
            && errno == EINVAL,
            "eventlog_entry_dumps (evf, NULL, ...) returns EINVAL");
        ok ((eventlog_entry_dumps (NULL, &error, good) == NULL)
            && errno == EINVAL,
            "eventlog_entry_dumps (NULL, &error, event) returns EINVAL");
        ok ((eventlog_entry_dumps (evf, &error, bad) == NULL)
            && errno == EINVAL,
            "eventlog_entry_dumps (NULL, &error, event) returns EINVAL");
        is (error.text, "eventlog_entry_parse: Invalid argument");

        ok (eventlog_formatter_set_format (evf, "json") == 0,
            "eventlog_formatter_set_format json");
    }

    json_decref (good);
    json_decref (bad);
    eventlog_formatter_destroy (evf);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    /*  Set TZ=UTC for predictable results in human-readable timestamps */
    setenv ("TZ", "", 1);
    tzset ();

    test_invalid ();
    test_basic ();

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

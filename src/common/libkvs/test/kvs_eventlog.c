/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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

#include "kvs.h"
#include "src/common/libtap/tap.h"

const char *badevent[] = {
    "1 foo",
    "1 foo bar",
    "1 foo bar bar",
    "x foo\n",
    "foo\n",
    "1 foo\nbar\n",
    "1\nfoo bar\n",
    "1\n foo\n",
    "\n1 foo\n",
    "1\n",
    "1 \n",
    "1  \n",
    "\n",
    "1 xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",
    NULL,
};

const char *badlog[] = {
    "\n",
    "1 foo",
    "1 foo\n\n",
    "\n1 foo\n",
    "1\n1\n",
    NULL,
};

const char *long_context = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

/* make output double the size of input to be safe */
char *printable (char *output, const char *input)
{
    const char *ip;
    char *op;

    for (ip = input, op = output; *ip != '\0'; ip++, op++) {
        switch (*ip) {
            case '\n':
                *op++ = '\\';
                *op = 'n';
                break;
            case '\r':
                *op++ = '\\';
                *op = 'r';
                break;
            default:
                *op = *ip;
        }
    }
    *op = '\0';
    return output;
}

void event_check (const char *s, double xtimestamp, const char *xname, const char *xcontext)
{
    int rc;
    double timestamp;
    char name[FLUX_KVS_MAX_EVENT_NAME + 1];
    char context[FLUX_KVS_MAX_EVENT_CONTEXT + 1];

    name[0] = '\0';
    context[0] = '\0';

    rc = flux_kvs_event_decode (s, &timestamp, name, sizeof (name),
                                context, sizeof (context));
    ok (rc == 0
        && timestamp == xtimestamp
        && (!xname || !strcmp (name, xname))
        && (!xcontext || !strcmp (context, xcontext)),
        "flux_kvs_event_decode time=%llu name=%s context=%s",
        (unsigned long long)xtimestamp, xname, xcontext);
}

void basic_check (struct flux_kvs_eventlog *log, bool first, bool xeof,
                  double xtimestamp, const char *xname, const char *xcontext)
{
    const char *s;

    if (first)
        s = flux_kvs_eventlog_first (log);
    else
        s = flux_kvs_eventlog_next (log);
    if (xeof) {
        ok (s == NULL,
            "flux_kvs_eventlog_%s = NULL", first ? "first" : "next");
    }
    else {
        ok (s != NULL,
            "flux_kvs_eventlog_%s != NULL", first ? "first" : "next");
        event_check (s, xtimestamp, xname, xcontext);
    }
}

void basic (void)
{
    const char *test1 = "42.123 foo\n44.0 bar quick brown fox\n";
    const char *test2 = "50 meep\n";
    const char *test3 = "60 mork mindy\n70 duh\n";
    struct flux_kvs_eventlog *log;
    char *s;

    /* simple create/destroy */
    log = flux_kvs_eventlog_create ();
    ok (log != NULL,
        "flux_kvs_eventlog_create works");
    flux_kvs_eventlog_destroy (log);

    /* create log from data and iterate */
    log = flux_kvs_eventlog_decode (test1);
    ok (log != NULL,
        "flux_kvs_eventlog_decode works on 2 entry log: [foo, bar]");

    basic_check (log, true, false, 42.123, "foo", "");
    basic_check (log, false, false, 44.0, "bar", "quick brown fox");
    basic_check (log, false, true, 0, NULL, NULL);

    /* encode and compare to input */
    s = flux_kvs_eventlog_encode (log);
    ok (s != NULL && !strcmp (s, test1),
        "flux_kvs_eventlog_encode output = decode input");
    free (s);

    /* append and iterate */
    ok (flux_kvs_eventlog_append (log, test2) == 0,
        "flux_kvs_eventlog_append works adding 1 entry: [foo, bar, meep]");
    ok (flux_kvs_eventlog_append (log, test3) == 0,
        "flux_kvs_eventlog_append works adding 2 entries: [foo, bar, meep, mork, duh]");

    basic_check (log, false, false, 50, "meep", "");
    basic_check (log, false, false, 60, "mork", "mindy");
    basic_check (log, false, false, 70, "duh", "");
    basic_check (log, false, true, 0, NULL, NULL);

    flux_kvs_eventlog_destroy (log);
}

void bad_input (void)
{
    struct flux_kvs_eventlog *log;
    char *s;
    int i;
    char buf[512];

    lives_ok ({flux_kvs_eventlog_destroy (NULL);},
              "flux_kvs_eventlog_destroy log=NULL doesn't crash");

    /* empty logs */
    errno = 0;
    ok (flux_kvs_eventlog_decode (NULL) == NULL && errno == EINVAL,
        "flux_kvs_eventlog_decode log=NULL fails with EINVAL");
    log = flux_kvs_eventlog_decode ("");
    ok (log != NULL && flux_kvs_eventlog_first (log) == NULL,
        "flux_kvs_eventlog_decode log=\"\" creates valid empty log");
    errno = 0;
    ok (flux_kvs_eventlog_append (log, "") == 0
        && flux_kvs_eventlog_first (log) == NULL,
        "flux_kvs_eventlog_append s=\"\" works, log still empty");
    s = flux_kvs_eventlog_encode (log);
    ok (s != NULL && !strcmp (s, ""),
        "flux_kvs_eventlog_encode returns \"\"");
    free (s);
    flux_kvs_eventlog_destroy (log);

    /* append */
    log = flux_kvs_eventlog_create ();
    if (!log)
        BAIL_OUT ("flux_kvs_eventlog_create failed");
    errno = 0;
    ok (flux_kvs_eventlog_append (NULL, "0 foo\n") < 0 && errno == EINVAL,
        "flux_kvs_eventlog_append log=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_eventlog_append (log, NULL) < 0 && errno == EINVAL,
        "flux_kvs_eventlog_append event=NULL fails with EINVAL");

    /* first/next */
    ok (flux_kvs_eventlog_first (NULL) == NULL,
        "flux_kvs_eventlog_first log=NULL returns NULL");
    ok (flux_kvs_eventlog_next (NULL) == NULL,
        "flux_kvs_eventlog_next log=NULL returns NULL");

    errno = 0;
    ok (flux_kvs_event_decode (NULL, NULL, NULL, 0, NULL, 0) < 0
        && errno == EINVAL,
        "flux_kvs_event_decode log=NULL fails with EINVAL");

    /* decode bad events */
    for (i = 0; badevent[i] != NULL; i++) {
        errno = 0;
        ok (flux_kvs_event_decode (badevent[i], NULL, NULL, 0, NULL, 0) < 0
            && errno == EINVAL,
            "flux_kvs_event_decode event=\"%s\" fails with EINVAL",
            printable (buf, badevent[i]));
        errno = 0;
        ok (flux_kvs_eventlog_append (log, badevent[i]) < 0
            && errno == EINVAL,
            "flux_kvs_eventlog_append event=\"%s\" fails with EINVAL",
            printable (buf, badevent[i]));
    }

    /* decode additional bad event (doesn't apply to
     * flux_kvs_eventlog_append() in loop above)
     */
    errno = 0;
    ok (flux_kvs_event_decode ("", NULL, NULL, 0, NULL, 0) < 0
        && errno == EINVAL,
        "flux_kvs_event_decode event=\"\" fails with EINVAL");

    /* decode bad logs */
    for (i = 0; badlog[i] != NULL; i++) {
        errno = 0;
        ok (flux_kvs_eventlog_decode (badlog[i]) == NULL && errno == EINVAL,
            "flux_kvs_eventlog_decode log=\"%s\" fails with EINVAL",
            printable (buf, badlog[i]));
    }

    errno = 0;
    ok (flux_kvs_eventlog_encode (NULL) == NULL && errno == EINVAL,
        "flux_kvs_eventlog_encode log=NULL fails with EINVAL");
}

void event (void)
{
    char *s;

    s = flux_kvs_event_encode_timestamp (1., "foo", NULL);
    ok (s != NULL,
        "flux_kvs_event_encode_timestamp context=NULL works");
    event_check (s, 1., "foo", NULL);
    free (s);

    s = flux_kvs_event_encode_timestamp (1., "foo", "foo");
    ok (s != NULL,
        "flux_kvs_event_encode_timestamp context=\"foo\" works");
    event_check (s, 1., "foo", "foo");
    free (s);

    s = flux_kvs_event_encode ("foo", "foo");
    ok (s != NULL,
        "flux_kvs_event_encode works");
    // no event_check(), can't predict timestamp
    free (s);

    errno = 0;
    ok (flux_kvs_event_encode_timestamp (-1., "foo", NULL) == NULL
        && errno == EINVAL,
        "flux_kvs_event_encode_timestamp timestamp=(-1) fails with EINVAL");

    errno = 0;
    ok (flux_kvs_event_encode_timestamp (1., "", NULL) == NULL
        && errno == EINVAL,
        "flux_kvs_event_encode_timestamp name=\"\" fails with EINVAL");

    errno = 0;
    ok (flux_kvs_event_encode_timestamp (1., "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", NULL) == NULL
        && errno == EINVAL,
        "flux_kvs_event_encode_timestamp name=(too long) fails with EINVAL");

    errno = 0;
    ok (flux_kvs_event_encode_timestamp (1., "a a", NULL) == NULL
        && errno == EINVAL,
        "flux_kvs_event_encode_timestamp name=\"a a\" fails with EINVAL");

    errno = 0;
    ok (flux_kvs_event_encode_timestamp (1., "a", "foo\n") == NULL
        && errno == EINVAL,
        "flux_kvs_event_encode_timestamp context=\"foo\\n\" fails with EINVAL");

    errno = 0;
    ok (flux_kvs_event_encode_timestamp (1., "a", long_context) == NULL
        && errno == EINVAL,
        "flux_kvs_event_encode_timestamp context=(too long) fails with EINVAL");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic ();
    bad_input ();
    event ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */


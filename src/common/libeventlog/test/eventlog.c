/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "src/common/libtap/tap.h"
#include "src/common/libeventlog/eventlog.h"

void eventlog_entry_parsing (void)
{
    json_t *event;
    double timestamp;
    const char *name;
    json_t *context;
    json_t *jstr;
    const char *str;

    errno = 0;
    ok (eventlog_entry_parse (NULL, NULL, NULL, NULL) < 0
        && errno == EINVAL,
        "eventlog_entry_parse fails with EINVAL on bad input");

    event = json_pack ("{ s:s }", "foo", "bar");
    if (!event)
        BAIL_OUT ("Error creating test json object");

    errno = 0;
    ok (eventlog_entry_parse (event, NULL, NULL, NULL) < 0
        && errno == EINVAL,
        "eventlog_entry_parse fails with EINVAL on bad event");
    json_decref (event);

    event = json_pack ("{ s:f s:s }",
                       "timestamp", 42.0,
                       "name", "foo");
    if (!event)
        BAIL_OUT ("Error creating test json object");

    ok (eventlog_entry_parse (event, &timestamp, &name, &context) == 0
        && timestamp == 42.0
        && !strcmp (name, "foo")
        && !context,
        "eventlog_entry_parse on event w/o context works");
    json_decref (event);

    event = json_pack ("{ s:f s:s s:{ s:s } }",
                       "timestamp", 52.0,
                       "name", "bar",
                       "context",
                       "foo", "bar");
    if (!event)
        BAIL_OUT ("Error creating test json object");

    ok (eventlog_entry_parse (event, &timestamp, &name, &context) == 0
        && timestamp == 52.0
        && !strcmp (name, "bar")
        && context
        && json_is_object (context)
        && (jstr = json_object_get (context, "foo"))
        && (str = json_string_value (jstr))
        && !strcmp (str, "bar"),
        "eventlog_entry_parse on event w/ context works");
    json_decref (event);
}

const char *goodevent[] = {
    "{\"timestamp\":42.0,\"name\":\"foo\"}\n",
    "{\"timestamp\":42.0,\"name\":\"foo\",\"context\":{\"bar\":16}}\n",
    NULL
};

const char *badevent[] = {
    "\n",
    "\n\n",
    "foo",
    "foo\n",
    /* no newline end */
    "{\"timestamp\":42.0,\"name\":\"foo\"}",
    /* no newline end */
    "{\"timestamp\":42.0,\"name\":\"foo\",\"context\":{\"bar\":16}}",
    /* double newline end */
    "{\"timestamp\":42.0,\"name\":\"foo\"}\n\n",
    /* prefix newline */
    "\n{\"timestamp\":42.0,\"name\":\"foo\"}",
    /* timestamp bad */
    "{\"timestamp\":\"foo\",\"name\":\"foo\"}\n",
    /* name bad */
    "{\"timestamp\":42.0,\"name\":18}\n",
    /* no name field */
    "{\"timestamp\":42.0}\n",
    /* no timestamp field */
    "{\"name\":\"foo\"}\n",
    /* context not object */
    "{\"timestamp\":42.0,\"name\":\"foo\",\"context\":\"bar\"}",
    NULL
};

const char *goodlog[] = {
    "",                         /* empty log is acceptable */
    "{\"timestamp\":42.0,\"name\":\"foo\"}\n{\"timestamp\":42.0,\"name\":\"foo\"}\n",
    "{\"timestamp\":42.0,\"name\":\"foo\"}\n{\"timestamp\":42.0,\"name\":\"foo\",\"context\":{\"bar\":16}}\n",
    NULL,
};

const char *badlog[] = {
    /* no newline between events */
    "{\"timestamp\":42.0,\"name\":\"foo\"}{\"timestamp\":42.0,\"name\":\"foo\"}\n",
    /* double newline between events */
    "{\"timestamp\":42.0,\"name\":\"foo\"}\n\n{\"timestamp\":42.0,\"name\":\"foo\"}\n",
    NULL,
};

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

void eventlog_decoding (void)
{
    char buf[512];
    json_t *o;
    int i;

    /* all good events are good logs */
    for (i = 0; goodevent[i] != NULL; i++) {
        o = eventlog_decode (goodevent[i]);
        ok (o != NULL,
            "eventlog_decode event=\"%s\" success",
            printable (buf, goodevent[i]));
        json_decref (o);
    }

    for (i = 0; goodlog[i] != NULL; i++) {
        o = eventlog_decode (goodlog[i]);
        ok (o != NULL,
            "eventlog_decode event=\"%s\" success",
            printable (buf, goodlog[i]));
        json_decref (o);
    }
}

void eventlog_decoding_errors (void)
{
    char buf[512];
    int i;

    errno = EINVAL;
    ok (eventlog_decode (NULL) == NULL
        && errno == EINVAL,
        "eventlog_decode fails with EINVAL on bad input");

    /* all bad events are also bad logs */
    for (i = 0; badevent[i] != NULL; i++) {
        errno = 0;
        ok (eventlog_decode (badevent[i]) == NULL
            && errno == EINVAL,
            "eventlog_decode event=\"%s\" fails with EINVAL",
            printable (buf, badevent[i]));
    }

    for (i = 0; badlog[i] != NULL; i++) {
        errno = 0;
        ok (eventlog_decode (badlog[i]) == NULL
            && errno == EINVAL,
            "eventlog_decode log=\"%s\" fails with EINVAL",
            printable (buf, badlog[i]));
    }
}

void eventlog_entry_decoding (void)
{
    char buf[512];
    json_t *o;
    int i;

    for (i = 0; goodevent[i] != NULL; i++) {
        o = eventlog_entry_decode (goodevent[i]);
        ok (o != NULL,
            "eventlog_entry_decode event=\"%s\" success",
            printable (buf, goodevent[i]));
        json_decref (o);
    }
}

void eventlog_entry_decoding_errors (void)
{
    char buf[512];
    int i;

    errno = 0;
    ok (eventlog_entry_decode (NULL) == NULL
        && errno == EINVAL,
        "eventlog_entry_decode fails with EINVAL on bad input");

    /* special case - empty string is bad input */
    errno = 0;
    ok (eventlog_entry_decode ("") == NULL
        && errno == EINVAL,
        "eventlog_entry_decode event=\"\" fails with EINVAL");

    for (i = 0; badevent[i] != NULL; i++) {
        errno = 0;
        ok (eventlog_entry_decode (badevent[i]) == NULL
            && errno == EINVAL,
            "eventlog_entry_decode event=\"%s\" fails with EINVAL",
            printable (buf, badevent[i]));
    }
}

void eventlog_entry_check (json_t *entry, double xtimestamp, const char *xname,
                           const char *xcontext)
{
    char *s;
    json_t *e;
    double timestamp;
    const char *name;
    json_t *context;
    char *context_str = NULL;

    /* could pass entry directly into eventlog_entry_parse(), but
     * we'll go through an encode/decode to make sure those functions
     * work correctly too */

    s = eventlog_entry_encode (entry);
    ok (s != NULL,
        "eventlog_entry_encode - encoded entry correctly");

    e = eventlog_entry_decode (s);
    ok (e != NULL,
        "eventlog_entry_decode - decoded \"%s\" correctly", s);

    ok (eventlog_entry_parse (e, &timestamp, &name, &context) == 0,
        "eventlog_entry_parse - decoded event successfully");

    if (context)
        context_str = json_dumps (context, JSON_COMPACT);

    ok ((xtimestamp == 0. || timestamp == xtimestamp)
        && (!xname || !strcmp (name, xname))
        && ((!xcontext && !context)
            || (context_str && !strcmp (context_str, xcontext))),
        "eventlog_entry_parse time=%llu name=%s context=%s",
        (unsigned long long)xtimestamp, xname, xcontext);

    free (context_str);
    json_decref (e);
    free (s);
}

json_t *test_eventlog_entry_vpack (double timestamp,
                                   const char *name,
                                   const char *fmt, ...)
{
    va_list ap;
    json_t *e;

    va_start (ap, fmt);
    e = eventlog_entry_vpack (timestamp, name, fmt, ap);
    va_end (ap);
    return e;
}

void eventlog_entry_encoding (void)
{
    json_t *e;

    e = eventlog_entry (0., "foo", NULL);
    ok (e != NULL,
        "eventlog_entry timestamp=0. works");
    eventlog_entry_check (e, 0., "foo", NULL);
    json_decref (e);

    e = eventlog_entry (1., "foo", NULL);
    ok (e != NULL,
        "eventlog_entry context=NULL works");
    eventlog_entry_check (e, 1., "foo", NULL);
    json_decref (e);

    e = eventlog_entry (1., "foo", "{\"data\":\"foo\"}");
    ok (e != NULL,
        "eventlog_entry context=\"{\"data\":\"foo\"}\" works");
    eventlog_entry_check (e, 1., "foo", "{\"data\":\"foo\"}");
    json_decref (e);

    e = eventlog_entry_pack (1., "foo", NULL);
    ok (e != NULL,
        "eventlog_entry_pack context=NULL works");
    eventlog_entry_check (e, 1., "foo", NULL);
    json_decref (e);

    e = eventlog_entry_pack (1., "foo", "{ s:s }", "data", "foo");
    ok (e != NULL,
        "eventlog_entry_pack context=\"{\"data\":\"foo\"}\" works");
    eventlog_entry_check (e, 1., "foo", "{\"data\":\"foo\"}");
    json_decref (e);

    e = test_eventlog_entry_vpack (1., "foo", NULL);
    ok (e != NULL,
        "eventlog_entry_vpack context=NULL works");
    eventlog_entry_check (e, 1., "foo", NULL);
    json_decref (e);

    e = test_eventlog_entry_vpack (1., "foo", "{ s:s }", "data", "foo");
    ok (e != NULL,
        "eventlog_entry_vpack context=\"{\"data\":\"foo\"}\" works");
    eventlog_entry_check (e, 1., "foo", "{\"data\":\"foo\"}");
    json_decref (e);
}

void eventlog_entry_encoding_errors (void)
{
    errno = 0;
    ok (eventlog_entry_encode (NULL) == NULL,
        "eventlog_entry_encode fails with EINVAL on bad input");

    errno = 0;
    ok (eventlog_entry (1., NULL, NULL) == NULL
        && errno == EINVAL,
        "eventlog_entry name=NULL fails with EINVAL");

    errno = 0;
    ok (eventlog_entry (1., "", NULL) == NULL
        && errno == EINVAL,
        "eventlog_entry name=\"\" fails with EINVAL");

    errno = 0;
    ok (eventlog_entry (1., "a a", NULL) == NULL
        && errno == EINVAL,
        "eventlog_entry name=\"a a\" fails with EINVAL");

    errno = 0;
    ok (eventlog_entry (1., "foo\n", NULL) == NULL
        && errno == EINVAL,
        "eventlog_entry name=\"foo\\n\" fails with EINVAL");

    errno = 0;
    ok (eventlog_entry (1., "foo", "") == NULL
        && errno == EINVAL,
        "eventlog_entry context=\"\" fails with EINVAL");

    errno = 0;
    ok (eventlog_entry (1., "foo", "foo") == NULL
        && errno == EINVAL,
        "eventlog_entry context=\"foo\" fails with EINVAL");

    errno = 0;
    ok (eventlog_entry (1., "foo", "[\"foo\"]") == NULL
        && errno == EINVAL,
        "eventlog_entry context=\"[\"foo\"]\" fails with EINVAL");

    errno = 0;
    ok (eventlog_entry (1., "foo", "{\"data\":\"foo\"}\n") == NULL
        && errno == EINVAL,
        "eventlog_entry context=\"{\"data\":\"foo\"}\\n\" fails with EINVAL");

    errno = 0;
    ok (eventlog_entry_pack (1., NULL, NULL) == NULL
        && errno == EINVAL,
        "eventlog_entry_pack name=NULL fails with EINVAL");

    errno = 0;
    ok (eventlog_entry_pack (1., "foo", "") == NULL
        && errno == EINVAL,
        "eventlog_entry_pack context=\"\" fails with EINVAL");

    errno = 0;
    ok (eventlog_entry_pack (1., "foo", "foo") == NULL
        && errno == EINVAL,
        "eventlog_entry_pack context=\"foo\" fails with EINVAL");

    errno = 0;
    ok (eventlog_entry_pack (1., "foo", "[\"foo\"]") == NULL
        && errno == EINVAL,
        "eventlog_entry_pack context=\"[\"foo\"]\" fails with EINVAL");

    errno = 0;
    ok (eventlog_entry_pack (1., "foo", "{\"data\":\"foo\"}") == NULL
        && errno == EINVAL,
        "eventlog_entry_pack context=\"{\"data\":\"foo\"}\" fails with EINVAL");

    errno = 0;
    ok (eventlog_entry_pack (1., "foo", "{ s:s }\n", "data", "foo") == NULL
        && errno == EINVAL,
        "eventlog_entry_pack context=\"{\"data\":\"foo\"}\\n\" fails with EINVAL");

    errno = 0;
    ok (test_eventlog_entry_vpack (1., NULL, NULL) == NULL
        && errno == EINVAL,
        "eventlog_entry_vpack name=NULL fails with EINVAL");

    errno = 0;
    ok (test_eventlog_entry_vpack (1., "foo", "") == NULL
        && errno == EINVAL,
        "eventlog_entry_vpack context=\"\" fails with EINVAL");

    errno = 0;
    ok (test_eventlog_entry_vpack (1., "foo", "foo") == NULL
        && errno == EINVAL,
        "eventlog_entry_vpack context=\"foo\" fails with EINVAL");

    errno = 0;
    ok (test_eventlog_entry_vpack (1., "foo", "[\"foo\"]") == NULL
        && errno == EINVAL,
        "eventlog_entry_vpack context=\"[\"foo\"]\" fails with EINVAL");

    errno = 0;
    ok (test_eventlog_entry_vpack (1., "foo", "{\"data\":\"foo\"}") == NULL
        && errno == EINVAL,
        "eventlog_entry_vpack context=\"{\"data\":\"foo\"}\" fails with EINVAL");

    errno = 0;
    ok (test_eventlog_entry_vpack (1., "foo", "{ s:s }\n", "data", "foo") == NULL
        && errno == EINVAL,
        "eventlog_entry_vpack context=\"{\"data\":\"foo\"}\\n\" fails with EINVAL");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    eventlog_entry_parsing ();
    eventlog_decoding ();
    eventlog_decoding_errors ();
    eventlog_entry_decoding ();
    eventlog_entry_decoding_errors ();
    eventlog_entry_encoding ();
    eventlog_entry_encoding_errors ();

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

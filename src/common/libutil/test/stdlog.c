/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include "src/common/libtap/tap.h"
#include "src/common/libutil/wallclock.h"
#include "src/common/libutil/stdlog.h"

#include <string.h>
#include <ctype.h>

static char *valid[] = {
    "<1>1 - - - - - - message",
    "<23>1 - - - - - - message",
    "<234>111 - - - - - - message",
    "<234>111 - - - - - - message",
    "<42>1 1985-04-12T23:20:50.52Z - - - - - message",
    "<42>1 1985-04-12T19:20:50.52-04:00 - - - - - message",
    "<42>1 2003-10-11T22:14:15.003Z - - - - - message",
    "<42>1 2003-08-24T05:14:15.000003-07:00 - - - - - message",
    "<42>1 2016-06-12T22:59:59.816857Z - - - - - message",
    "<42>1 2016-06-12T22:59:59.816857Z 0 - - - - message",
    "<42>1 2016-06-12T22:59:59.816857Z 1 - - - - message",
    "<42>1 2016-06-12T22:59:59.816857Z 4294967295 - - - - message",
    "<42>1 2016-06-12T22:59:59.816857Z this-is-a-really-long-hostname-field-well-we-have-255-chars-avaialable-so-maybe-not-that-long-huh - - - - message",
    "<42>1 2016-06-12T22:59:59.816857Z 0 logger - - - message",
    "<42>1 2016-06-12T22:59:59.816857Z 0 procid-000@@@-aaa - - - message",
    "<42>1 2016-06-12T22:59:59.816857Z 0 logger procid - - message",
    "<42>1 2016-06-12T22:59:59.816857Z 0 logger - msgid - message",
    "<42>1 2016-06-12T22:59:59.816857Z 0 logger - msgid [exampleSDID@32473 iut=\"3\" eventSource=\"Application\" eventID=\"1011\"] message",
    "<42>1 2016-06-12T22:59:59.816857Z 0 logger - msgid [exampleSDID@32473 iut=\"3\" eventSource=\"Application\" eventID=\"1011\"][examplePriority@32473 class=\"high\"] message",
    "<42>1 2016-06-12T22:59:59.816857Z 0 logger - msgid [exampleSDID@32473 iut=\"3\" eventSource=\"Application\" eventID=\"1011\"][examplePriority@32473 class=\"high\"] message",
    NULL,
};

void test_split (void)
{
    char buf[2048];
    char *xtra;
    struct stdlog_header hdr;
    int len;
    int msglen;
    const char *msg;

    stdlog_init (&hdr);

    /* orig=foo\nbar\nbaz */
    len = stdlog_encode (buf, sizeof (buf), &hdr, STDLOG_NILVALUE,
                         "foo\nbar\nbaz");
    ok (len >= 0,
        "stdlog_encode encoded foo\\nbar\\nbaz");
    xtra = stdlog_split_message (buf, &len, "\r\n");
    ok (xtra != NULL && !strcmp (xtra, "bar\nbaz"),
        "stdlog_split_message got bar\\nbaz");
    ok (stdlog_decode (buf, len, &hdr, NULL, NULL, &msg, &msglen) == 0
        && msg != NULL && msglen == 3 && !strncmp (msg, "foo", msglen),
        "and truncated orig message to foo");

    /* xtra=bar\nbaz */
    len = stdlog_encode (buf, sizeof (buf), &hdr, STDLOG_NILVALUE, xtra);
    ok (len >= 0,
        "stdlog_encode encoded bar\\nbaz");
    free (xtra);
    xtra = stdlog_split_message (buf, &len, "\r\n");
    ok (xtra != NULL && !strcmp (xtra, "baz"),
        "stdlog_split_message got baz");
    diag ("xtra='%s'", xtra);
    ok (stdlog_decode (buf, len, &hdr, NULL, NULL, &msg, &msglen) == 0
        && msg != NULL && msglen == 3 && !strncmp (msg, "bar", msglen),
        "and truncated orig message to bar");

    /* xtra=baz */
    len = stdlog_encode (buf, sizeof (buf), &hdr, STDLOG_NILVALUE, xtra);
    ok (len >= 0,
        "stdlog_encode encoded baz");
    free (xtra);
    xtra = stdlog_split_message (buf, &len, "\r\n");
    ok (xtra == NULL,
        "stdlog_split message got NULL");
}

int main(int argc, char** argv)
{
    char buf[2048];
    struct stdlog_header hdr, cln;
    int n, len;
    const char *sd, *msg;
    int sdlen, msglen;

    plan (NO_PLAN);

    stdlog_init (&hdr);
    len = stdlog_encode (buf, sizeof (buf), &hdr,
                         STDLOG_NILVALUE, STDLOG_NILVALUE);
    ok (len >= 0,
        "stdlog_init encoded defaults");
    diag ("%.*s", len, buf);

    /* Ensure that decode reverses encode for default case
     */
    memset (&hdr, 0, sizeof (hdr));
    n = stdlog_decode (buf, len, &hdr, &sd, &sdlen, &msg, &msglen);
    ok (n == 0,
        "stdlog_decode worked on encoded buf");
    stdlog_init (&cln);
    ok (hdr.pri == cln.pri,
        "stdlog_decode decoded pri");
    ok (hdr.version == cln.version,
        "stdlog_decode decoded version");
    ok (strcmp (hdr.timestamp, cln.timestamp) == 0,
        "stdlog_decode decoded timestamp");
    ok (strcmp (hdr.hostname, cln.hostname) == 0,
        "stdlog_decode decoded hostname") ;
    ok (strcmp (hdr.appname, cln.appname) == 0,
        "stdlog_decode decoded appname") ;
    ok (strcmp (hdr.procid, cln.procid) == 0,
        "stdlog_decode decoded procid") ;
    ok (strcmp (hdr.msgid, cln.msgid) == 0,
        "stdlog_decode decoded msgid") ;
    ok (sdlen == strlen (STDLOG_NILVALUE) && strncmp (sd, STDLOG_NILVALUE, sdlen) == 0,
        "stdlog_decode decoded structured data");
    ok (msglen == strlen (STDLOG_NILVALUE) && strncmp (msg, STDLOG_NILVALUE, msglen) == 0,
        "stdlog_decode decoded message");

    /* Check that trailing \n or \r in message are dropped
     */
    stdlog_init (&hdr);
    len = stdlog_encode (buf, sizeof (buf), &hdr,
                         STDLOG_NILVALUE,
                         "Hello whorl\n\r\n");
    ok (len >= 0,
        "stdlog_encode worked with message");
    diag ("%.*s", len, buf);
    n = stdlog_decode (buf, len, &hdr, &sd, &sdlen, &msg, &msglen);
    ok (n == 0 && strncmp (msg, "Hello whorl", msglen) == 0,
        "trailing cr/lf chars were truncated");

    int i = 0;
    while (valid[i] != NULL) {
        n = stdlog_decode (valid[i], strlen (valid[i]), &hdr, &sd, &sdlen, &msg, &msglen);
        ok (n == 0 && msglen == strlen ("message") && strncmp (msg, "message", msglen) == 0,
            "successfully decoded %s", valid[i]);
        i++;
    }

    test_split ();

    done_testing();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

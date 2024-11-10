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

#include "src/common/libtap/tap.h"
#include "src/common/libutil/timestamp.h"

struct test_entry {
    const char *entry;
    time_t ts;
    int sec;
    int min;
    int hour;
    int mday;
    int mon;
    int year;
    suseconds_t us;
};

/* N.B.: All expected outputs assume TZ=PST8PDT
 */
struct test_entry tests[] = {
    { "2017-03-17T04:11:45.948349Z",
      1489723905,
      45, 11, 21, 16, 3, 2017, 948349
    },
    { "2020-06-05T23:34:22.960708Z",
      1591400062,
      22, 34, 16, 5, 6, 2020, 960708
    },
    { "1977-10-18T15:30:37.53737Z",
      246036637,
      37, 30, 8, 18, 10, 1977, 537370
    },
    { "1971-11-02T15:18:03.191981Z",
      57943083,
      3, 18, 7, 2, 11, 1971, 191981
    },
    { "1996-12-17T15:23:31.253948Z",
      850836211,
      31, 23, 7, 17, 12, 1996, 253948
    },
    { "2013-10-11T11:46:10.907826Z",
      1381491970,
      10, 46, 4, 11, 10, 2013, 907826
    },
    { "2011-02-03T07:44:19.881821Z",
      1296719059,
      19, 44, 23, 2, 2, 2011, 881821
    },
    { "1979-07-28T05:59:14.035254Z",
      301989554,
      14, 59, 22, 27, 7, 1979, 35254
    },
    { "1977-10-22T14:17:21.905639Z",
      246377841,
      21, 17, 7, 22, 10, 1977, 905639
    },
    { "2013-02-27T20:00:39.353657Z",
      1361995239,
      39, 0, 12, 27, 2, 2013, 353657
    },
    { "2023-04-08T23:14:34.029081Z",
      1680995674,
      34, 14, 16, 8, 4, 2023, 29081
    },
    { "2013-01-29T02:36:38.527697Z",
      1359426998,
      38, 36, 18, 28, 1, 2013, 527697
    },
    { "1996-11-12T23:58:38.277011Z",
      847843118,
      38, 58, 15, 12, 11, 1996, 277011
    },
    { "2007-01-27T18:13:58.749355Z",
      1169921638,
      58, 13, 10, 27, 1, 2007, 749355
    },
    { "1985-01-11T05:51:23.032399Z",
      474270683,
      23, 51, 21, 10, 1, 1985, 32399
    },
    { "1971-06-26T06:41:19.743417Z",
      46766479,
      19, 41, 23, 25, 6, 1971, 743417
    },
    { "1996-08-05T05:31:01.268064Z",
      839223061,
      1, 31, 22, 4, 8, 1996, 268064
    },
    { "2000-02-23T12:13:17.427706Z",
      951307997,
      17, 13, 4, 23, 2, 2000, 427706
    },
    { "1985-04-07T00:31:25.608501Z",
      481681885,
      25, 31, 16, 6, 4, 1985, 608501
    },
    { "1970-04-21T12:58:31.529143Z",
      9550711,
      31, 58, 4, 21, 4, 1970, 529143
    },
    { "1978-11-22T13:16:29.795159Z",
      280588589,
      29, 16, 5, 22, 11, 1978, 795159
    },
    { "1984-11-07T12:10:05.840087Z",
      468677405,
      5, 10, 4, 7, 11, 1984, 840087
    },
    { "1987-11-06T22:33:15.153931Z",
      563236395,
      15, 33, 14, 6, 11, 1987, 153931
    },
    { "1979-11-23T00:55:52.367158Z",
      312166552,
      52, 55, 16, 22, 11, 1979, 367158
    },
    { "1972-10-19T17:02:31.682269Z",
      88362151,
      31, 2, 10, 19, 10, 1972, 682269
    },
    { "2001-12-27T10:13:29.52Z",
      1009448009,
      29, 13, 2, 27, 12, 2001, 520000
    },
    { "1984-10-30T10:49:56.3Z",
      467981396,
      56, 49, 2, 30, 10, 1984, 300000
    },
    { "1989-04-14T05:06:09.000003Z",
      608533569,
      9, 6, 22, 13, 4, 1989, 3
    },
    { "1983-03-16T23:04:03.00003Z",
      416703843,
      3, 4, 15, 16, 3, 1983, 30
    },
    { "1988-05-11T02:47:16.003Z",
      579322036,
      16, 47, 19, 10, 5, 1988, 3000
    },
    { "1970-01-01T00:00:00.836367Z",
      0,
      0, 0, 16, 31, 12, 1969, 836367
    },
    { "1970-01-01T00:00:00.000000Z",
      0,
      0, 0, 16, 31, 12, 1969, 0
    },
    { "2011-08-28T16:30:40.000000Z",
      1314549040,
      40, 30, 9, 28, 8, 2011, 0
    },
    { "1970-01-01T00:00:00Z",
      0,
      0, 0, 16, 31, 12, 1969, 0
    },
    { "2017-01-14T05:18:47Z",
      1484371127,
      47, 18, 21, 13, 1, 2017, 0
    },
    { NULL, 0, 0, 0, 0, 0, 0, 0, 0 }
};

static void test_invalid ()
{
    struct tm tm;
    struct timeval tv;
    struct test_entry *te = &tests[0];

    ok (timestamp_parse ("", &tm, &tv) < 0,
        "timestamp_parse empty string fails");
    ok (timestamp_parse ("1:00", &tm, &tv) < 0,
        "timestamp_parse on invalid timestamp fails");
    ok (timestamp_parse ("1969-01-01T00:00:00Z", &tm, &tv) < 0,
        "timestamp_parse on too old timestamp fails");

    ok (timestamp_parse (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "timestamp_parse (NULL, NULL, NULL) fails with EINVAL");
    ok (timestamp_parse (NULL, &tm, &tv) < 0 && errno == EINVAL,
        "timestamp_parse (NULL, &tm, &tv) fails with EINVAL");
    ok (timestamp_parse (te->entry, NULL, NULL) < 0 && errno == EINVAL,
        "timestamp_parse (NULL, &tm, &tv) fails with EINVAL");

    ok (timestamp_from_double (-1., &tm, &tv) < 0 && errno == EINVAL,
        "timestamp_from_double (-1, &tm, &tv) fails with EINVAL");
    ok (timestamp_from_double (0., NULL, NULL) < 0 && errno == EINVAL,
        "timestamp_from_double (0., NULL, NULL) fails with EINVAL");

    ok (timestamp_parse (te->entry, &tm, NULL) == 0,
        "timestamp_parse (ts, &tm, NULL) works");
    ok (tm.tm_year == te->year - 1900
        && tm.tm_mon == te->mon - 1
        && tm.tm_mday == te->mday
        && tm.tm_min == te->min
        && tm.tm_sec == te->sec,
        "timestamp is expected values");

    ok (timestamp_parse (te->entry, NULL, &tv) == 0,
        "timestamp_parse (ts, NULL, &tv) works");
    ok (tv.tv_sec == te->ts
        && tv.tv_usec == te->us,
        "timsestamp is expected value");
}

static void test_entry_check (struct test_entry *test,
                              struct tm tm,
                              struct timeval tv)
{
    ok (tm.tm_sec == test->sec,
        "tm_sec == %d (expected %d)", tm.tm_sec, test->sec);
    ok (tm.tm_min == test->min,
        "tm_min == %d (expected %d)", tm.tm_min, test->min);
    /* N.B.: We do not test tm_hour since this may be influenced
     * by incorrect, missing, or updated DST values in the local
     * system's tzdata.
    ok (tm.tm_mday == test->mday,
        "tm_mday == %d (expected %d)", tm.tm_mday, test->mday);
     */
    /* tm_mon is months since Jan 0-11
     */
    ok (tm.tm_mon == test->mon - 1,
        "tm_mon == %d (expected %d)", tm.tm_mon, test->mon - 1);
    /* tm_year is number of years since 1900
     */
    ok (tm.tm_year == test->year - 1900,
        "tm_year == %d (expected %d)", tm.tm_year, test->year - 1900);

    ok (tv.tv_sec == test->ts,
        "tv_sec == %u (expected %u)", tv.tv_sec, test->ts);
    ok (tv.tv_usec == test->us,
        "tv_usec == %u (expected %u)", tv.tv_usec, test->us);
}

static void test_all ()
{
    struct test_entry *test;
    struct tm tm;
    struct timeval tv;

    test = tests;
    while (test->entry) {
        char buf[1024];
        ok (timestamp_parse (test->entry, &tm, &tv) == 0,
            "timestamp_parse: %s", test->entry);
        timestamp_tostr ((time_t) tv.tv_sec, buf, sizeof (buf));
        diag ("%s", buf);
        test_entry_check (test, tm, tv);

        /* Now test timestamp_from_double()
         */
        double ts = tv.tv_sec + ((double) tv.tv_usec / 1000000);

        memset (&tm, 0, sizeof (tm));
        memset (&tv, 0, sizeof (tv));
        ok (timestamp_from_double (ts, &tm, &tv) == 0,
            "timestamp_from_double (%f) works",
            ts);
        test_entry_check (test, tm, tv);

        ++test;
    }
}

void test_tzoffset (void)
{
    struct tm tm;

    ok (timestamp_tzoffset (NULL, NULL, 0) < 0 && errno == EINVAL,
        "timestamp_tzoffset (NULL, NULL, 0) returns EINVAL");

    memset (&tm, 0, sizeof (tm));
    ok (timestamp_tzoffset (&tm, NULL, 0) < 0 && errno == EINVAL,
        "timestamp_tzoffset (&tm, NULL, 0) returns EINVAL");
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    /* All expected outputs assume a timezone of PST8PDT */
    setenv ("TZ", "PST8PDT", 1);

    test_all ();
    test_invalid ();
    test_tzoffset ();

    done_testing ();
}

// vi:ts=4 sw=4 expandtab

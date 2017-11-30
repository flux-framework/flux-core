
#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <errno.h>
#include <string.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/cronodate.h"

static bool string_to_tm (char *s, struct tm *tmp)
{
    char *p = strptime (s,"%Y-%m-%d %H:%M:%S", tmp);
    if (p == NULL || *p != '\0')
        return (false);
    return (true);
}

static bool string_to_tv (char *s, struct timeval *tvp)
{
    time_t t;
    struct tm tm;
    char *p = strptime (s, "%Y-%m-%d %H:%M:%S", &tm);

    if ((t = mktime (&tm)) == (time_t) -1)
        return (false);

    tvp->tv_sec = t;
    tvp->tv_usec = 0;
    if (*p == '.') {
        char *q;
        double d = strtod (p, &q);
        if (*q != '\0') {
            diag ("Failed to convert usecs %s", p);
            return (false);
        }
        tvp->tv_usec = (long) (d * 1.0e6);
    }
    return (true);
}

static bool cronodate_check_match (struct cronodate *d, char *s)
{
    struct tm tm;
    ok (string_to_tm (s, &tm), "string_to_tm (%s)", s);
    return cronodate_match (d, &tm);
}

static bool cronodate_check_next (struct cronodate *d,
                                  char *start, char *expected)
{
    char buf [256];
    time_t t, t_exp;
    struct tm tm;
    int rc;

    ok (string_to_tm (expected, &tm),
        "string_to_tm (expected=%s)", expected);
    if ((t_exp = mktime (&tm)) < (time_t) 0)
        return false;

    ok (string_to_tm (start, &tm),
        "string_to_tm (start=%s)", start);
    strftime (buf, sizeof (buf), "%Y-%m-%d %H:%M:%S %Z", &tm);
    diag ("start = %s", buf);
    rc = cronodate_next (d, &tm);
    strftime (buf, sizeof (buf), "%Y-%m-%d %H:%M:%S %Z", &tm);
    ok (rc >= 0, "cronodate_next() = %s", buf);
    if ((t = mktime (&tm)) < (time_t) 0) {
        diag ("mktime: %s", strerror (errno));
        return false;
    }
    diag ("expected %d, got  %d", t_exp, t);
    return (t == t_exp);
}

static double tv_to_double (struct timeval *tv)
{
    return (tv->tv_sec + (tv->tv_usec/1e6));
}

static bool almost_is (double a, double b)
{
    return fabs (a - b) < 1e-5;
}

int main (int argc, char *argv[])
{
    int i;
    int rc;
    double x;
    struct tm tm;
    struct timeval tv;
    struct cronodate *d;

    plan (NO_PLAN);

    // Force TZ to GMT
    setenv ("TZ", "", 1);

    ok (tm_unit_min (TM_SEC) == 0, "check min value for tm_sec");
    ok (tm_unit_min (TM_MIN) == 0, "check min value for tm_min");
    ok (tm_unit_min (TM_HOUR) == 0, "check min value for tm_hour");
    ok (tm_unit_min (TM_MON) == 0, "check min value for tm_mon");
    ok (tm_unit_min (TM_YEAR) == 0, "check min value for tm_year");
    ok (tm_unit_min (TM_WDAY) == 0, "check min value for tm_wday");
    ok (tm_unit_min (TM_MDAY) == 1, "check min value for tm_mday");

    ok (tm_unit_max (TM_SEC) == 60, "check max value for tm_sec");
    ok (tm_unit_max (TM_MIN) == 59, "check max value for tm_min");
    ok (tm_unit_max (TM_HOUR) == 23, "check max value for tm_hour");
    ok (tm_unit_max (TM_MON) == 11, "check max value for tm_mon");
    ok (tm_unit_max (TM_YEAR) == 3000-1900, "check max value for tm_year");
    ok (tm_unit_max (TM_WDAY) == 6, "check max value for tm_wday");
    ok (tm_unit_max (TM_MDAY) == 31, "check max value for tm_mday");

    is (tm_unit_string (TM_SEC),  "second", "tm_unit_string: seconds");
    is (tm_unit_string (TM_MIN),  "minute", "tm_unit_string: minute");
    is (tm_unit_string (TM_HOUR), "hour", "tm_unit_string: hour");
    is (tm_unit_string (TM_MON),  "month", "tm_unit_string: month");
    is (tm_unit_string (TM_MDAY), "mday", "tm_unit_string: mday");
    is (tm_unit_string (TM_WDAY), "weekday", "tm_unit_string: weekday");
    is (tm_unit_string (TM_YEAR), "year", "tm_unit_string: year");

    for (i = 0; i < 12; i++)
        ok (tm_string_to_month (tm_month_string (i)) == i, "checking %s", tm_month_string (i));
    for (i = 0; i < 7; i++)
        ok (tm_string_to_weekday (tm_weekday_string (i)) == i, "checking %s", tm_weekday_string (i));

    ok (tm_string_to_month ("foo") == -1, "invalid month returns -1");
    ok (tm_string_to_weekday ("foo") == -1, "invalid weekday returns -1");
    ok (tm_month_string (12) == NULL, "invalid month returns NULL");
    ok (tm_weekday_string (8) == NULL, "invalid weekday returns NULL");

    /* Basic functionality tests */
    d = cronodate_create ();

    /* test ranges, keywords */
    ok (cronodate_set (d, TM_MON, "Jan") >= 0, "set Jan");
    is (cronodate_get (d, TM_MON), "0",  "got '0'");
    ok (cronodate_set (d, TM_MON, "*/2") >= 0, "set mon = '*/2'");
    is (cronodate_get (d, TM_MON), "0,2,4,6,8,10",  "got every other month");
    ok (cronodate_set (d, TM_MON, "1-5,7-9") >= 0, "set mon = '1-5,7-9'");
    is (cronodate_get (d, TM_MON), "1-5,7-9",  "got '1-5,7-9'");
    ok (cronodate_set (d, TM_MON, "January-June") >= 0, "set January to June");
    is (cronodate_get (d, TM_MON), "0-5", "get January to June");

    ok (cronodate_set (d, TM_MON, "Mars") < 0, "bad month fails as expected");
    ok (cronodate_set (d, TM_MON, "-/") < 0, "bad range fails as expected");

    ok (cronodate_set (d, TM_WDAY, "Mon") >= 0, "set Mon");
    is (cronodate_get (d, TM_WDAY), "1", "get Mon");
    ok (cronodate_set (d, TM_WDAY, "*/2") >= 0, "set mon = '*/2'");
    is (cronodate_get (d, TM_WDAY), "0,2,4,6",  "got every second day");
    ok (cronodate_set (d, TM_WDAY, "mon-fri") >= 0, "set mon-fri");
    is (cronodate_get (d, TM_WDAY), "1-5",  "got 0-5");

    ok (d != NULL, "cronodate_create()");
    /* match all dates */
    cronodate_fillset (d);
    ok (cronodate_check_match (d, "2001-01-01 12:45:33"), "date matches after fillset");

    ok (cronodate_set (d, TM_SEC, "5") >= 0, "cronodate_set, sec=5");
    ok (cronodate_check_match (d, "2001-10-10 00:00:05"), "date matches");
    ok (!cronodate_check_match (d, "2001-10-10 00:00:06"), "date doesn't match");

    ok (cronodate_set (d, TM_MIN, "5") >= 0, "cronodate_set, min=5");
    ok (cronodate_check_match (d, "2001-10-10 00:05:05"), "date matches");
    ok (!cronodate_check_match (d, "2001-10-10 00:06:05"), "date doesn't match");

    ok (cronodate_set (d, TM_HOUR, "5") >= 0, "cronodate_set, hour=5");
    ok (cronodate_check_match (d, "2001-10-10 05:05:05"), "date matches");
    ok (!cronodate_check_match (d, "2001-10-10 06:05:05"), "date doesn't match");

    ok (cronodate_set (d, TM_MDAY, "10") >= 0, "cronodate_set, mday = 10");
    ok (cronodate_check_match (d, "2001-10-10 05:05:05"), "date matches");
    ok (!cronodate_check_match (d, "2001-10-11 05:05:05"), "date doesn't match");

    ok (cronodate_set (d, TM_MON, "9") >= 0, "cronodate_set MON=9 (Oct)");
    ok (cronodate_check_match (d, "2001-10-10 05:05:05"), "date matches");
    ok (!cronodate_check_match (d, "2001-01-10 05:05:05"), "date doesn't match");

    cronodate_fillset (d);

    // Set up for next midnight
    ok (cronodate_set (d, TM_SEC, "0") >= 0, "date glob set, sec = 0");
    ok (cronodate_set (d, TM_MIN, "0") >= 0, "date glob set, min = 0");
    ok (cronodate_set (d, TM_HOUR, "0") >= 0, "date glob set, hour = 0");
    ok (cronodate_check_next (d, "2016-05-27 3:45:22", "2016-05-28 00:00:00"),
        "cronodate_next returned next midnight");

    ok (cronodate_check_next (d, "2016-12-31 3:45:22", "2017-01-01 00:00:00"),
        "cronodate_next rolled over to following year");

    cronodate_fillset (d);
    // Run every 10 min on 5s
    ok (cronodate_set (d, TM_SEC, "5") >= 0, "set sec = 5");
    ok (cronodate_set (d, TM_MIN, "5,15,25,35,45,55") >= 0, "set sec = 5");
    ok (cronodate_check_next (d, "2016-10-10 3:00:00", "2016-10-10 3:05:05"),
        "cronodate_next worked for minutes");
    ok (cronodate_check_next (d, "2016-10-10 3:05:05", "2016-10-10 3:15:05"),
        "cronodate_next worked for next increment");

    cronodate_fillset (d);
    // Run every monday, 8am
    ok (cronodate_set (d, TM_SEC, "0") >= 0, "date glob set, sec = 0");
    ok (cronodate_set (d, TM_MIN, "0") >= 0, "date glob set, min = 0");
    ok (cronodate_set (d, TM_HOUR, "8") >= 0, "date glob set, hour = 0");
    ok (cronodate_set (d, TM_WDAY, "1") >= 0, "date glob set, wday = 1 (Mon)");
    ok (cronodate_check_next (d, "2016-06-01 10:45:00", "2016-06-06 08:00:00"),
        "cronodate_next worked for next monday");
    ok (cronodate_check_next (d, "2016-06-06 08:00:00", "2016-06-13 08:00:00"),
        "cronodate_next returns next matching date when current matches ");

    cronodate_fillset (d);
    // Same as above, but use cronodate_set_integer()
    ok (cronodate_set_integer (d, TM_SEC, 0) >= 0, "set integer, sec = 0");
    ok (cronodate_set_integer (d, TM_MIN, 0) >= 0, "set integer, min = 0");
    ok (cronodate_set_integer (d, TM_HOUR, 8) >= 0, "set integer, hour = 0");
    ok (cronodate_set_integer (d, TM_WDAY, 1) >= 0, "set integer, wday = 1 (Mon)");
    ok (cronodate_check_next (d, "2016-06-01 10:45:00", "2016-06-06 08:00:00"),
        "cronodate_next worked for next monday");
    ok (cronodate_check_next (d, "2016-06-06 08:00:00", "2016-06-13 08:00:00"),
        "cronodate_next returns next matching date when current matches ");

    // ERANGE test for cronodate_set_integer
    ok (cronodate_set_integer (d, TM_SEC, -1) < 0 && errno == ERANGE,
        "TM_SEC == -1 returns ERANGE");
    ok (cronodate_set_integer (d, TM_SEC, 61) < 0 && errno == ERANGE,
        "TM_SEC == 61 returns ERANGE");
    ok (cronodate_set_integer (d, TM_MIN, -1) < 0 && errno == ERANGE,
        "TM_MIN == -1 returns ERANGE");
    ok (cronodate_set_integer (d, TM_MIN, 60) < 0 && errno == ERANGE,
        "TM_MIN == 60 returns ERANGE");
    ok (cronodate_set_integer (d, TM_HOUR, -1) < 0 && errno == ERANGE,
        "TM_HOUR == 0 returns ERANGE");
    ok (cronodate_set_integer (d, TM_HOUR, 24) < 0 && errno == ERANGE,
        "TM_HOUR == 24 returns ERANGE");
    ok (cronodate_set_integer (d, TM_WDAY, -1) < 0 && errno == ERANGE,
        "TM_WDAY == -1 returns ERANGE");
    ok (cronodate_set_integer (d, TM_WDAY, 7) < 0 && errno == ERANGE,
        "TM_WDAY == 24 returns ERANGE");
    ok (cronodate_set_integer (d, TM_MON, -1) < 0 && errno == ERANGE,
        "TM_MON == -1 returns ERANGE");
    ok (cronodate_set_integer (d, TM_MON, 12) < 0 && errno == ERANGE,
        "TM_MON == 12 returns ERANGE");
    ok (cronodate_set_integer (d, TM_MDAY, 0) < 0 && errno == ERANGE,
        "TM_MDAY == 0 returns ERANGE");
    ok (cronodate_set_integer (d, TM_MDAY, 32) < 0 && errno == ERANGE,
        "TM_MDAY == 32 returns ERANGE");
    ok (cronodate_set_integer (d, TM_YEAR, -1) < 0 && errno == ERANGE,
        "TM_YEAR == -1 returns ERANGE");
    ok (cronodate_set_integer (d, TM_YEAR, 3001-1900) < 0 && errno == ERANGE,
        "TM_YEAR == %d returns ERANGE", 3001-1900);

    ok (cronodate_set (d, TM_MON, "6") >= 0, "date glob set, mon = 6");
    ok (cronodate_set (d, TM_MDAY, "6") >= 0, "date glob set, mday = 6");
    ok (cronodate_set (d, TM_YEAR, "*") >= 0, "date glob set, year = *");

    // Impossible date returns error
    ok (string_to_tm ("2016-06-06 08:00:00", &tm), "string_to_tm");
    rc = cronodate_next (d, &tm);
    ok (rc < 0, "cronodate_next() fails when now is >= matching date");

    cronodate_fillset (d);
    // test cronodate_remaining ()
    ok (cronodate_set (d, TM_SEC, "0") >= 0, "date glob set, sec = 0");
    ok (cronodate_set (d, TM_MIN, "0") >= 0, "date glob set, min = 0");
    ok (cronodate_set (d, TM_HOUR, "8") >= 0, "date glob set, hour = 0");
    ok (string_to_tv ("2016-06-06 07:00:00.3", &tv), "string_to_tv");

    x = cronodate_remaining (d, tv_to_double (&tv));
    ok (almost_is (x, 3599.700), "cronodate_remaining works: got %.6fs", x);
    ok (string_to_tv ("2016-06-06 08:00:00", &tv), "string_to_tv");
    x = cronodate_remaining (d, tv_to_double (&tv));
    ok (almost_is (x, 24*60*60), "cronodate_remaining works: got %.6fs", x);

    cronodate_destroy (d);

    done_testing ();
    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */


#define _XOPEN_SOURCE
#include <time.h>
#include <sys/time.h>
#include <math.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/approxidate.h"
#include "src/common/libutil/cronodate.h"

static int approxidate_tm (char *s, struct tm *tm)
{
    struct timeval tv;
    time_t t;
    if (approxidate (s, &tv) < 0)
        return (-1);
    t = tv.tv_sec;
    if (localtime_r (&t, tm) == NULL)
        return (-1);
    return (0);
}

static bool cronodate_check_match (struct cronodate *d, char *s)
{
    struct tm tm;
    ok (approxidate_tm (s, &tm) >= 0, "approxidate (%s)", s);
    return cronodate_match (d, &tm);
}

static bool cronodate_check_next (struct cronodate *d,
                                  char *start, char *expected)
{
    char buf [256];
    time_t t, t_exp;
    struct tm tm;
    int rc;

    ok (approxidate_tm (expected, &tm) >= 0,
        "approxidate (expected=%s)", expected);
    if ((t_exp = mktime (&tm)) < (time_t) 0)
        return false;

    ok (approxidate_tm (start, &tm) >= 0,
        "approxidate (start=%s)", start);
    rc = cronodate_next (d, &tm);
    strftime (buf, sizeof (buf), "%Y-%m-%d %H:%M:%S", &tm);
    ok (rc >= 0, "cronodate_next() = %s", buf);
    if ((t = mktime (&tm)) < (time_t) 0)
        return false;
    return (t == t_exp);
}

static double tv_to_double (struct timeval *tv)
{
    return (tv->tv_sec + (tv->tv_usec/1e6));
}

static bool almost_is (double a, double b)
{
    return fabs (a - b) < 1e-6;
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
    ok (cronodate_check_match (d, "1/1/2001 12:45:33"), "date matches after fillset");
    
    ok (cronodate_set (d, TM_SEC, "5") >= 0, "cronodate_set, sec=5");
    ok (cronodate_check_match (d, "oct 10, 2001 00:00:05"), "date matches");
    ok (!cronodate_check_match (d, "oct 10, 2001 00:00:06"), "date doesn't match");

    ok (cronodate_set (d, TM_MIN, "5") >= 0, "cronodate_set, min=5");
    ok (cronodate_check_match (d, "oct 10, 2001 00:05:05"), "date matches");
    ok (!cronodate_check_match (d, "oct 10, 2001 00:06:05"), "date doesn't match");

    ok (cronodate_set (d, TM_HOUR, "5") >= 0, "cronodate_set, hour=5");
    ok (cronodate_check_match (d, "oct 10, 2001 05:05:05"), "date matches");
    ok (!cronodate_check_match (d, "oct 10, 2001 06:05:05"), "date doesn't match");

    ok (cronodate_set (d, TM_MDAY, "10") >= 0, "cronodate_set, mday = 10");
    ok (cronodate_check_match (d, "oct 10, 2001 05:05:05"), "date matches");
    ok (!cronodate_check_match (d, "oct 11, 2001 05:05:05"), "date doesn't match");

    ok (cronodate_set (d, TM_MON, "9") >= 0, "cronodate_set MON=9 (Oct)");
    ok (cronodate_check_match (d, "oct 10, 2001 05:05:05"), "date matches");
    ok (!cronodate_check_match (d, "jan 10, 2001 05:05:05"), "date doesn't match");

    cronodate_fillset (d);

    // Set up for next midnight
    ok (cronodate_set (d, TM_SEC, "0") >= 0, "date glob set, sec = 0");
    ok (cronodate_set (d, TM_MIN, "0") >= 0, "date glob set, min = 0");
    ok (cronodate_set (d, TM_HOUR, "0") >= 0, "date glob set, hour = 0");
    ok (cronodate_check_next (d, "may 27, 2016 3:45:22", "may 28, 2016 00:00:00"),
        "cronodate_next returned next midnight");
    
    ok (cronodate_check_next (d, "dec 31, 2016 3:45:22", "jan 1, 2017 00:00:00"),
        "cronodate_next rolled over to following year");

    cronodate_fillset (d);
    // Run every 10 min on 5s
    ok (cronodate_set (d, TM_SEC, "5") >= 0, "set sec = 5");
    ok (cronodate_set (d, TM_MIN, "5,15,25,35,45,55") >= 0, "set sec = 5");
    ok (cronodate_check_next (d, "oct 10, 2016 3:00:00", "oct 10, 2016 3:05:05"),
        "cronodate_next worked for minutes");
    ok (cronodate_check_next (d, "oct 10, 2016 3:05:05", "oct 10, 2016 3:15:05"),
        "cronodate_next worked for next increment");

    cronodate_fillset (d);
    // Run every monday, 8am
    ok (cronodate_set (d, TM_SEC, "0") >= 0, "date glob set, sec = 0");
    ok (cronodate_set (d, TM_MIN, "0") >= 0, "date glob set, min = 0");
    ok (cronodate_set (d, TM_HOUR, "8") >= 0, "date glob set, hour = 0");
    ok (cronodate_set (d, TM_WDAY, "1") >= 0, "date glob set, wday = 1 (Mon)");
    ok (cronodate_check_next (d, "jun 1, 2016 10:45:00", "jun 06, 2016 08:00:00"),
        "cronodate_next worked for next monday");
    ok (cronodate_check_next (d, "jun 06, 2016 08:00:00", "jun 13, 2016 08:00:00"),
        "cronodate_next returns next matching date when current matches ");

    ok (cronodate_set (d, TM_MON, "6") >= 0, "date glob set, mon = 6");
    ok (cronodate_set (d, TM_MDAY, "6") >= 0, "date glob set, mday = 6");
    ok (cronodate_set (d, TM_YEAR, "*") >= 0, "date glob set, year = *");

    // Impossible date returns error
    ok (approxidate_tm ("jun 06, 2016 08:00:00", &tm) >= 0, "approxidate");
    rc = cronodate_next (d, &tm);
    ok (rc < 0, "cronodate_next() fails when now is >= matching date");

    cronodate_fillset (d);
    // test cronodate_remaining ()
    ok (cronodate_set (d, TM_SEC, "0") >= 0, "date glob set, sec = 0");
    ok (cronodate_set (d, TM_MIN, "0") >= 0, "date glob set, min = 0");
    ok (cronodate_set (d, TM_HOUR, "8") >= 0, "date glob set, hour = 0");
    ok (approxidate ("jun 06, 2016 07:00:00.3", &tv) >= 0, "approxidate");
    
    x = cronodate_remaining (d, tv_to_double (&tv));
    ok (almost_is (x, 3599.700), "cronodate_remaining works: got %.3fs", x);
    ok (approxidate ("jun 06, 2016 08:00:00.0", &tv) >= 0, "approxidate");
    x = cronodate_remaining (d, tv_to_double (&tv));
    ok (almost_is (x, 24*60*60), "cronodate_remaining works: got %.3fs", x);

    cronodate_destroy (d);

    done_testing ();
    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */

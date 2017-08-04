
#ifndef HAVE_CRONODATE_H
#define HAVE_CRONODATE_H 1

#include <time.h>
#include <sys/types.h>
#include <stdbool.h>

/* Set of time units supported for cronodate */
typedef enum {
    TM_SEC,
    TM_MIN,
    TM_HOUR,
    TM_MDAY,
    TM_MON,
    TM_YEAR,
    TM_WDAY,
    TM_MAX_ITEM
} tm_unit_t;

typedef struct cronodate cronodate_t;

/*
 *  Return max or min value for time unit u
 */
int tm_unit_max (tm_unit_t u);
int tm_unit_min (tm_unit_t u);
const char *tm_unit_string (tm_unit_t u);

int tm_string_to_weekday (const char *day);
const char *tm_weekday_string (int w);

int tm_string_to_month (const char *month);
const char *tm_month_string (int w);

/* Create an empty cronodate object that will not
 *  match any date-time
 */
cronodate_t * cronodate_create (void);
void cronodate_destroy (cronodate_t *);

/*  Fill cronodate structure will all values, or
 *   empty an existing cronodate struct.
 */
void cronodate_fillset (cronodate_t *);
void cronodate_emptyset (cronodate_t *);

/*  Set cronodate field for time unit u to a range
 *  Returns EINVAL if any value of range is outside [min, max] for
 *  time unit u, or numeric members of range cannot be parsed.
 *
 *  `range` supports single values, e.g. '5', ranges '2-5', and
 *  comma separated lists of ranges '0,2-5'. For Weekday and month
 *  ranges, named day/months are also accepted, e.g.  'Mon-Fri' or
 *  'Jan,Mar-Jun'.
 *
 *  A range of '*' is used to indicate 'MIN-MAX' for the specified
 *  time unit `u`.
 *
 *  A range may also be followed by '/N' where N is the stride,
 *  e.g. 'x-y/2' indicates every other number in the range x-y.
 */
int cronodate_set (cronodate_t *d, tm_unit_t u, const char *range);

/*  Set cronodate field for time unit u to a single value
 *  Returns EINVAL if value is outside of [min, max] for time unit u.
 *  Returns 0 on success.
 */
int cronodate_set_integer (cronodate_t *d, tm_unit_t u, int value);

/*  Get the current set/range for time unit `u` in cronodate object `d`
 *   in the form of a nodeset range string.
 */
const char *cronodate_get (cronodate_t *d, tm_unit_t u);

/*
 *  Return true if broken down time struct `tm` matches the cronodate
 *   expression in `m`.
 */
bool cronodate_match (cronodate_t *d, struct tm *tm);

/*
 *  Advance `now` to the next date/time that will match `m`.
 */
int cronodate_next (cronodate_t *d, struct tm *now);

/*
 *  Given epoch in floating points seconds `now` return the time
 *   remaining in floating point seconds until the next matching
 *   date in cronodate object `d`, or < 0.0 if no next time.
 */
double cronodate_remaining (cronodate_t *d, double now);

#endif /* !HAVE_CRONODATE_H */

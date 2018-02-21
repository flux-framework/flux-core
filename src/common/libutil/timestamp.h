#ifndef _UTIL_TIMESTAMP_H
#define _UTIL_TIMESTAMP_H

#include <time.h>

/* Convert time_t (GMT) to ISO 8601 timestamp string,
 * e.g. "2003-08-24T05:14:50Z"
 */
int timestamp_tostr (time_t t, char *buf, int size);

/* Convert from ISO 8601 string to time_t.
 */
int timestamp_fromstr (const char *s, time_t *tp);


#endif /* !_UTIL_TIMESTAMP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

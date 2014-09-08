#ifndef _UTIL_MONOTIME_H
#define _UTIL_MONOTIME_H

double monotime_since (struct timespec t0); /* milliseconds */
void monotime (struct timespec *tp);
bool monotime_isset (struct timespec t);

#endif /* !_UTIL_MONOTIME_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

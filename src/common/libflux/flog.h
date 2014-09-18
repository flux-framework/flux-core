#ifndef _FLUX_CORE_FLOG_H
#define _FLUX_CORE_FLOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <czmq.h>

#include "handle.h"

/* FIXME: redirect flag causes redirection to syslog if logging
 * is so configured, but facility and level are not passed to msg/err()
 * (see libutil/log.c)
 */

void flux_log_set_facility (flux_t h, const char *facility);
int flux_vlog (flux_t h, int lev, const char *fmt, va_list ap);
int flux_log (flux_t h, int lev, const char *fmt, ...)
            __attribute__ ((format (printf, 3, 4)));

/* If the redirect flag is set, we are the end of the line and any
 * log request messages should be decoded and "logged" (sent to
 * libutil/log.c's configurable destination).
 */
void flux_log_set_redirect (flux_t h, bool flag);

/* Log a cmb.log message to libutil/log.c.
 * The message is not destroyed.
 */
int flux_log_zmsg (zmsg_t *zmsg);

#endif /* !_FLUX_CORE_FLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

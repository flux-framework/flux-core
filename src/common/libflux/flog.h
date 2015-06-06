#ifndef _FLUX_CORE_FLOG_H
#define _FLUX_CORE_FLOG_H

#include <stdarg.h>
#include <stdbool.h>

#include "handle.h"

/* Set log facility for handle instance.
 */
void flux_log_set_facility (flux_t h, const char *facility);

/* Inject a message into the logging system.
 */
int flux_vlog (flux_t h, int lev, const char *fmt, va_list ap);
int flux_log (flux_t h, int lev, const char *fmt, ...)
            __attribute__ ((format (printf, 3, 4)));
int flux_log_json (flux_t h, const char *json_str);

/* If the redirect flag is set, we are the end of the line and any
 * log request messages should be decoded and "logged" (sent to
 * libutil/log.c's configurable destination).
 */
void flux_log_set_redirect (flux_t h, bool flag);

#endif /* !_FLUX_CORE_FLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

#ifndef _FLUX_CORE_FLOG_H
#define _FLUX_CORE_FLOG_H

#include <sys/time.h>
#include <stdarg.h>
#include <stdbool.h>

#include "handle.h"

typedef void (*flux_log_f)(void *ctx, const char *facility, int level,
                           int rank, struct timeval tv, const char *msg);

/* Set log facility for handle instance.
 */
void flux_log_set_facility (flux_t h, const char *facility);

/* Inject a message into the logging system.
 */
int flux_vlog (flux_t h, int lev, const char *fmt, va_list ap);
int flux_log (flux_t h, int lev, const char *fmt, ...)
            __attribute__ ((format (printf, 3, 4)));
int flux_log_json (flux_t h, const char *json_str);

/* Redirect log messages to flux_log_f in this handle instance.
 */
void flux_log_set_redirect (flux_t h, flux_log_f fun, void *ctx);

#endif /* !_FLUX_CORE_FLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

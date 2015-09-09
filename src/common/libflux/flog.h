#ifndef _FLUX_CORE_FLOG_H
#define _FLUX_CORE_FLOG_H

#include <sys/time.h>
#include <stdarg.h>
#include <stdbool.h>
#include <syslog.h>

#include "handle.h"

typedef void (*flux_log_f)(const char *facility, int level, uint32_t rank,
                           struct timeval tv, const char *msg, void *arg);

/* Set log facility for handle instance.
 * Unlike syslog(3), the flux log facility is an arbitrary string.
 */
void flux_log_set_facility (flux_t h, const char *facility);

/* Log a message at the specified level, as defined for syslog(3).
 */
int flux_vlog (flux_t h, int level, const char *fmt, va_list ap);
int flux_log (flux_t h, int level, const char *fmt, ...)
              __attribute__ ((format (printf, 3, 4)));

/* Log a message at LOG_ERR level, appending a colon, space, and error string.
 * The system 'errno' is assumed to be valid and contain an error code
 * that can be decoded with zmq_strerror(3).
 */
int flux_log_verror (flux_t h, const char *fmt, va_list ap);
int flux_log_error (flux_t h, const char *fmt, ...)
                 __attribute__ ((format (printf, 2, 3)));

#define FLUX_LOG_ERROR(h) \
    (void)flux_log_error ((h), "%s::%d[%s]", __FILE__, __LINE__, __FUNCTION__)

/* Log a message in its encoded JSON form.
 */
int flux_log_json (flux_t h, const char *json_str);

/* Redirect log messages to flux_log_f in this handle instance.
 */
void flux_log_set_redirect (flux_t h, flux_log_f fun, void *arg);


#endif /* !_FLUX_CORE_FLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

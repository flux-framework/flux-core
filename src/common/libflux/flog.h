#ifndef _FLUX_CORE_FLOG_H
#define _FLUX_CORE_FLOG_H

#include <sys/time.h>
#include <stdarg.h>
#include <stdbool.h>
#include <syslog.h>
#include <stdlib.h>
#include <errno.h>

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

/* Check for error return codes, if one is found log an error with log_error
 * and die with error code errno */
__attribute__ ((format (printf, 3, 4)))
int flux_log_check_int (flux_t h,
               int res,
               const char *fmt,
               ...  );

__attribute__ ((format (printf, 3, 4)))
void *flux_log_check_ptr (flux_t h,
                 void *res,
                 const char *fmt,
                 ... );

#define FLOG_REAL_STRINGIFY(X) #X
#define FLOG_STRINGIFY(X) FLOG_REAL_STRINGIFY (X)
#define FLOG_POSITION __FILE__ ":" FLOG_STRINGIFY (__LINE__)

// NOTE: FMT string *MUST* be a string literal
#define FLUX_CHECK_INT(H, X, ...) flux_log_check_int ((H), (X), FLOG_POSITION \
                                          ":negative integer from:" \
                                          FLOG_STRINGIFY (X) ":" \
                                          __VA_ARGS__)
#define FLUX_CHECK_PTR(H, X, ...) flux_log_check_ptr ((H), (X), FLOG_POSITION \
                                          ":null pointer from:" \
                                          FLOG_STRINGIFY (X) ":" \
                                          __VA_ARGS__)

#define FLUX_LOG_ERROR(h) \
    (void)flux_log_error ((h), "%s::%d[%s]", __FILE__, __LINE__, __FUNCTION__)

/* Redirect log messages to flux_log_f in this handle instance.
 */
void flux_log_set_redirect (flux_t h, flux_log_f fun, void *arg);


/* Manipulate the broker's ring buffer.
 */
enum {
    FLUX_DMESG_CLEAR = 1,
    FLUX_DMESG_FOLLOW = 2,
};

int flux_dmesg (flux_t h, int flags, flux_log_f fun, void *arg);

/* flux_log_f callback that prints a log message to a FILE stream
 * passed in as 'arg'.
 */
void flux_log_fprint (const char *facility, int level, uint32_t rank,
                      struct timeval tv, const char *message, void *arg);


#endif /* !_FLUX_CORE_FLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

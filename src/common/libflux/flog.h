/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_FLOG_H
#define _FLUX_CORE_FLOG_H

#include <stdarg.h>
#include <syslog.h>
#include <stdlib.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*flux_log_f)(const char *buf, int len, void *arg);

/* Set log appname for handle instance.
 * Value will be truncated after 48 characters (not including NUL).
 */
void flux_log_set_appname (flux_t *h, const char *s);

/* Set log procid for handle instance.
 * Value will be truncated after 128 characters (not including NUL).
 */
void flux_log_set_procid (flux_t *h, const char *s);

/* Set log hostname for handle instance.
 * Value will be truncated after 255 characters (not including NUL).
 * Set to NULL or empty string and flux_log() will call flux_get_rank()
 * and use that as the log hostname.
 */
void flux_log_set_hostname (flux_t *h, const char *s);

/* Log a message at the specified level, as defined for syslog(3).
 *
 * Flux handle is optional, if set to NULL output to stderr.
 */
int flux_vlog (flux_t *h, int level, const char *fmt, va_list ap);
int flux_log (flux_t *h, int level, const char *fmt, ...)
              __attribute__ ((format (printf, 3, 4)));

/* Log a message at LOG_ERR level, appending a colon, space, and error string.
 * The system 'errno' is assumed to be valid.
 *
 * Flux handle is optional, if set to NULL output to stderr.
 */
void flux_log_verror (flux_t *h, const char *fmt, va_list ap);
void flux_log_error (flux_t *h, const char *fmt, ...)
                 __attribute__ ((format (printf, 2, 3)));

#define FLUX_LOG_ERROR(h) \
    (void)flux_log_error ((h), "%s::%d[%s]", __FILE__, __LINE__, __FUNCTION__)

/* Redirect log messages to flux_log_f in this handle instance.
 */
void flux_log_set_redirect (flux_t *h, flux_log_f fun, void *arg);

/* Convert errno to string.
 * Flux errno space includes POSIX errno + zeromq errors.
 */
const char *flux_strerror (int errnum);

/* Flux log function compatible with libutil llog interface
 */
void flux_llog (void *arg,
                const char *file,
                int line,
                const char *func,
                const char *subsys,
                int level,
                const char *fmt,
                va_list ap);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_FLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

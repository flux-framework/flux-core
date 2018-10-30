/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#ifndef _FLUX_CORE_FLOG_H
#define _FLUX_CORE_FLOG_H

#include <stdarg.h>
#include <syslog.h>
#include <stdlib.h>
#include <errno.h>

#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif


#define FLUX_MAX_LOGBUF     2048

/* May be ored with 'level' to cause log request
 * to wait for a success/fail response.
 */
#define FLUX_LOG_CHECK      0x1000

typedef void (*flux_log_f)(const char *buf, int len, void *arg);

/* Set log appname for handle instance.
 * Value will be truncated after FLUX_MAX_APPNAME bytes.
 */
void flux_log_set_appname (flux_t *h, const char *s);

/* Set log procid for handle instance.
 * Value will be truncated after FLUX_MAX_PROCID bytes.
 */
void flux_log_set_procid (flux_t *h, const char *s);

/* Log a message at the specified level, as defined for syslog(3).
 *
 * Flux handle is optional, if set to NULL output to stderr.
 */
int flux_vlog (flux_t *h, int level, const char *fmt, va_list ap);
int flux_log (flux_t *h, int level, const char *fmt, ...)
              __attribute__ ((format (printf, 3, 4)));

/* Log a message at LOG_ERR level, appending a colon, space, and error string.
 * The system 'errno' is assumed to be valid and contain an error code
 * that can be decoded with zmq_strerror(3).
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


/* Manipulate the broker's ring buffer.
 */
enum {
    FLUX_DMESG_CLEAR = 1,
    FLUX_DMESG_FOLLOW = 2,
};

int flux_dmesg (flux_t *h, int flags, flux_log_f fun, void *arg);

/* flux_log_f callback that prints a log message to a FILE stream
 * passed in as 'arg'.
 */
void flux_log_fprint (const char *buf, int len, void *arg);

/* Convert errno to string.
 * Flux errno space includes POSIX errno + zeromq errors.
 */
const char *flux_strerror (int errnum);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_FLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

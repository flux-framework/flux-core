/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Flux internal library logging interface.
 *
 * Mostly taken from libtsm shl-llog.h, with updates for Flux
 * typedef naming styles.
 *
 * https://www.freedesktop.org/wiki/Software/kmscon/libtsm/
 *
 * SHL - Library Log/Debug Interface
 *
 * Copyright (c) 2010-2013 David Herrmann <dh.herrmann@gmail.com>
 * Dedicated to the Public Domain
 *
 * libtsm COPYRIGHT notice:

This software is licensed under the terms of the MIT license. Please see each
source file for the related copyright notice and license.

If a file does not contain a copyright notice, the following license shall
apply:

	Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>

	Permission is hereby granted, free of charge, to any person obtaining
	a copy of this software and associated documentation files
	(the "Software"), to deal in the Software without restriction, including
	without limitation the rights to use, copy, modify, merge, publish,
	distribute, sublicense, and/or sell copies of the Software, and to
	permit persons to whom the Software is furnished to do so, subject to
	the following conditions:

	The above copyright notice and this permission notice shall be included
	in all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
	OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
	CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
	TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
	SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

/*
 * Library Log/Debug Interface
 * Libraries should always avoid producing side-effects. This includes writing
 * log-messages of any kind. However, you often don't want to disable debugging
 * entirely, therefore, the core objects often contain a pointer to a function
 * which performs logging. If that pointer is NULL (default), logging is
 * disabled.
 *
 * This header should never be installed into the system! This is _no_ public
 * header. Instead, copy it into your application if you want and use it there.
 * Your public library API should include something like this:
 *
 *   typedef void (*MYPREFIX_log_t) (void *data,
 *                                   const char *file,
 *                                   int line,
 *                                   const char *func,
 *                                   const char *subs,
 *                                   unsigned int sev,
 *                                   const char *format,
 *                                   va_list args);
 *
 * And then the user can supply such a function when creating a new context
 * object of your library or simply supply NULL. Internally, you have a field of
 * type "MYPREFIX_log_t llog" in your main structure. If you pass this to the
 * convenience helpers like llog_dbg(), llog_warn() etc. it will automatically
 * use the "llog" field to print the message. If it is NULL, nothing is done.
 *
 * The arguments of the log-function are defined as:
 *   data: User-supplied data field that is passed straight through.
 *   file: Zero terminated string of the file-name where the log-message
 *         occurred. Can be NULL.
 *   line: Line number of @file where the message occurred. Set to 0 or smaller
 *         if not available.
 *   func: Function name where the log-message occurred. Can be NULL.
 *   subs: Subsystem where the message occurred (zero terminated). Can be NULL.
 *   sev: Severity of log-message. An integer between 0 and 7 as defined below.
 *        These are identical to the linux-kernel severities so there is no need
 *        to include these in your public API. Every app can define them
 *        themselves, if they need it.
 *   format: Format string. Must not be NULL.
 *   args: Argument array
 *
 * The user should also be able to optionally provide a data field which is
 * always passed unmodified as first parameter to the log-function. This allows
 * to add context to the logger.
 */

#ifndef _UTIL_LLOG_H
#define _UTIL_LLOG_H

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include "errno_safe.h"

enum llog_severity {
        LLOG_FATAL = 0,
        LLOG_ALERT = 1,
        LLOG_CRITICAL = 2,
        LLOG_ERROR = 3,
        LLOG_WARNING = 4,
        LLOG_NOTICE = 5,
        LLOG_INFO = 6,
        LLOG_DEBUG = 7,
        LLOG_SEV_NUM,
};

typedef void (*llog_writer_f) (void *arg,
                               const char *file,
                               int line,
                               const char *func,
                               const char *subsys,
                               int level,
                               const char *fmt,
                               va_list args);

static inline  __attribute__((format(printf, 8, 9)))
void llog_format (llog_writer_f llog,
                               void *arg,
                               const char *file,
                               int line,
                               const char *func,
                               const char *subsys,
                               int level,
                               const char *fmt,
                               ...)
{
    if (llog) {
        int saved_errno = errno;
        va_list ap;
        va_start (ap, fmt);
        errno = saved_errno;
        llog (arg, file, line, func, subsys, level, fmt, ap);
        va_end (ap);
        errno = saved_errno;
    }
}

#ifndef LLOG_SUBSYSTEM
static const char *LLOG_SUBSYSTEM __attribute__((__unused__));
#endif

#define LLOG_DEFAULT __FILE__, __LINE__, __func__, LLOG_SUBSYSTEM

#define llog_printf(obj, sev, format, ...) \
        llog_format((obj)->llog, \
                    (obj)->llog_data, \
                    LLOG_DEFAULT, \
                    (sev), \
                    (format), \
                    ##__VA_ARGS__)
#define llog_dprintf(obj, data, sev, format, ...) \
        llog_format((obj), \
                    (data), \
                    LLOG_DEFAULT, \
                    (sev), \
                    (format), \
                    ##__VA_ARGS__)

static inline __attribute__((format(printf, 4, 5)))
void llog_dummyf(llog_writer_f llog, void *data, unsigned int sev,
                 const char *format, ...)
{
}

/*
 * Helpers
 * They pick up all the default values and submit the message to the
 * llog-subsystem. The llog_debug() function will discard the message unless
 * LLOG_ENABLE_DEBUG is defined.
 */
#ifdef LLOG_ENABLE_DEBUG
        #define llog_ddebug(obj, data, format, ...) \
                llog_dprintf((obj), (data), LLOG_DEBUG, (format), ##__VA_ARGS__)
        #define llog_debug(obj, format, ...) \
                llog_ddebug((obj)->llog, (obj)->llog_data, (format), ##__VA_ARGS__)
#else
        #define llog_ddebug(obj, data, format, ...) \
                llog_dummyf((obj), (data), LLOG_DEBUG, (format), ##__VA_ARGS__)
        #define llog_debug(obj, format, ...) \
                llog_ddebug((obj)->llog, (obj)->llog_data, (format), ##__VA_ARGS__)
#endif


#define llog_info(obj, format, ...) \
        llog_printf((obj), LLOG_INFO, (format), ##__VA_ARGS__)
#define llog_dinfo(obj, data, format, ...) \
        llog_dprintf((obj), (data), LLOG_INFO, (format), ##__VA_ARGS__)
#define llog_notice(obj, format, ...) \
        llog_printf((obj), LLOG_NOTICE, (format), ##__VA_ARGS__)
#define llog_dnotice(obj, data, format, ...) \
        llog_dprintf((obj), (data), LLOG_NOTICE, (format), ##__VA_ARGS__)
#define llog_warning(obj, format, ...) \
        llog_printf((obj), LLOG_WARNING, (format), ##__VA_ARGS__)
#define llog_dwarning(obj, data, format, ...) \
        llog_dprintf((obj), (data), LLOG_WARNING, (format), ##__VA_ARGS__)
#define llog_error(obj, format, ...) \
        llog_printf((obj), LLOG_ERROR, (format), ##__VA_ARGS__)
#define llog_derror(obj, data, format, ...) \
        llog_dprintf((obj), (data), LLOG_ERROR, (format), ##__VA_ARGS__)
#define llog_critical(obj, format, ...) \
        llog_printf((obj), LLOG_CRITICAL, (format), ##__VA_ARGS__)
#define llog_dcritical(obj, data, format, ...) \
        llog_dprintf((obj), (data), LLOG_CRITICAL, (format), ##__VA_ARGS__)
#define llog_alert(obj, format, ...) \
        llog_printf((obj), LLOG_ALERT, (format), ##__VA_ARGS__)
#define llog_dalert(obj, data, format, ...) \
        llog_dprintf((obj), (data), LLOG_ALERT, (format), ##__VA_ARGS__)
#define llog_fatal(obj, format, ...) \
        llog_printf((obj), LLOG_FATAL, (format), ##__VA_ARGS__)
#define llog_dfatal(obj, data, format, ...) \
        llog_dprintf((obj), (data), LLOG_FATAL, (format), ##__VA_ARGS__)


#endif /* !_UTIL_LLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

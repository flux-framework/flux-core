#ifndef _UTIL_LOG_H
#define _UTIL_LOG_H

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

#include "macros.h"

void log_init (char *cmd_name);
void log_fini (void);

void log_err_exit (const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2), noreturn));
void log_err (const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2)));
void log_errn_exit (int errnum, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3), noreturn));
void log_errn (int errnum, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
void log_msg_exit (const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2), noreturn));
void log_msg (const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2)));


#endif /* !_UTIL_LOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

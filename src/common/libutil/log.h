#ifndef _UTIL_LOG_H
#define _UTIL_LOG_H

#include <errno.h>
#include <stdarg.h>

void log_init (char *p);
void log_fini (void);
void log_set_dest (char *dest);
char *log_get_dest (void);
void log_msg (const char *fmt, va_list ap);

void err_exit (const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2), noreturn));
void err (const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2)));
void errn_exit (int errnum, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3), noreturn));
void errn (int errnum, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
void msg_exit (const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2), noreturn));
void msg (const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2)));

#define oom() do { \
  errn_exit (ENOMEM, "%s::%s(), line %d", __FILE__, __FUNCTION__, __LINE__); \
} while (0)

const char *log_leveltostr (int level);
int log_strtolevel (const char *s);

#endif /* !_UTIL_LOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

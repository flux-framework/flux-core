#ifndef _UTIL_LOG_H
#define _UTIL_LOG_H

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

#include "macros.h"

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

int check_int (int res,
               const char *fmt,
               ...  )
__attribute__ ((format (printf, 2, 3)));
void *check_ptr (void *res,
                 const char *fmt,
                 ... )
__attribute__ ((format (printf, 2, 3)));

#define LOG_POSITION __FILE__ ":" STRINGIFY (__LINE__)

// NOTE: FMT string *MUST* be a string literal
#define CHECK_INT(X, ...) check_int ((X), LOG_POSITION \
                                          ":negative integer from:" \
                                          STRINGIFY (X) ":" \
                                          __VA_ARGS__)
#define CHECK_PTR(X, ...) check_ptr ((X), LOG_POSITION \
                                          ":null pointer from:" \
                                          STRINGIFY (X) ":" \
                                          __VA_ARGS__)

#define oom() do { \
  errn_exit (ENOMEM, "%s::%s(), line %d", __FILE__, __FUNCTION__, __LINE__); \
} while (0)

const char *log_leveltostr (int level);
int log_strtolevel (const char *s);

#endif /* !_UTIL_LOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

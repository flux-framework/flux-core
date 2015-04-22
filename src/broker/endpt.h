#ifndef _BROKER_ENDPT_H
#define _BROKER_ENDPT_H

#include <stdarg.h>

typedef struct {
    void *zs;
    char *uri;
} endpt_t;

endpt_t *endpt_vcreate (const char *fmt, va_list ap);
endpt_t *endpt_create (const char *fmt, ...)
                              __attribute__ ((format (printf, 1, 2)));
void endpt_destroy (endpt_t *ep);

#endif /* !_BROKER_ENDPT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

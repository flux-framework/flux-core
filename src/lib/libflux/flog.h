#ifndef _FLUX_FLOG_H
#define _FLUX_FLOG_H

void flux_log_set_facility (flux_t h, const char *facility);
int flux_vlog (flux_t h, int lev, const char *fmt, va_list ap);
int flux_log (flux_t h, int lev, const char *fmt, ...)
            __attribute__ ((format (printf, 3, 4)));

#endif /* !_FLUX_FLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

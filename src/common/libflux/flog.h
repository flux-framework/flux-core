#ifndef _FLUX_CORE_FLOG_H
#define _FLUX_CORE_FLOG_H

void flux_log_set_facility (flux_t h, const char *facility);
int flux_vlog (flux_t h, int lev, const char *fmt, va_list ap);
int flux_log (flux_t h, int lev, const char *fmt, ...)
            __attribute__ ((format (printf, 3, 4)));

/* If the redirect flag is set, we are the end of the line and any
 * log request messages should be decoded and "logged" (sent to
 * libutil/log.c's configurable destination).
 */
void flux_log_set_redirect (flux_t h, bool flag);

/* Forward or log (depending on redirect setting) a log request message.
 * The message is destroyed if successful.
 */
int flux_log_zmsg (flux_t h, zmsg_t **zmsg);

#endif /* !_FLUX_CORE_FLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

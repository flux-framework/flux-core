#ifndef FLUX_LOG_H
#define FLUX_LOG_H

void cmb_log_set_facility (flux_t h, const char *facility);
int cmb_vlog (flux_t h, int lev, const char *fmt, va_list ap);
int cmb_log (flux_t h, int lev, const char *fmt, ...)
            __attribute__ ((format (printf, 3, 4)));

int cmb_log_subscribe (flux_t h, int lev, const char *sub);
int cmb_log_unsubscribe (flux_t h, const char *sub);
int cmb_log_dump (flux_t h, int lev, const char *fac);

char *cmb_log_decode (zmsg_t *zmsg, int *lp, char **fp, int *cp,
                    struct timeval *tvp, char **sp);

#endif /* !FLUX_LOG_H */

/*
 *  * vi:tabstop=4 shiftwidth=4 expandtab
 *   */


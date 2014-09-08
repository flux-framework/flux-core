#ifndef _FLUX_PANIC_H
#define _FLUX_PANIC_H

int flux_panic (flux_t h, int rank, const char *msg);

void flux_assfail (flux_t h, char *ass, char *file, int line);
#define FASSERT(h, exp) if ((exp)); \
                        else flux_assfail(h, #exp, __FILE__, __LINE__)

#endif /* !_FLUX_PANIC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

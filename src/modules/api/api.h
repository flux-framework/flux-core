#ifndef FLUX_CMB_H
#define FLUX_CMB_H

flux_t flux_api_open (void);
flux_t flux_api_openpath (const char *path, int flags);

void flux_api_close (flux_t h);

#endif /* !FLUX_CMB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

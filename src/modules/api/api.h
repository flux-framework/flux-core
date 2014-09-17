#ifndef _FLUX_CORE_API_H
#define _FLUX_CORE_API_H

flux_t flux_api_open (void);
flux_t flux_api_openpath (const char *path, int flags);

void flux_api_close (flux_t h);

#endif /* !_FLUX_CORE_API_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

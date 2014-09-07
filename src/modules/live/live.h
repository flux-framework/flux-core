#ifndef FLUX_LIVE_H
#define FLUX_LIVE_H

int flux_failover (flux_t h, int rank);
int flux_recover (flux_t h, int rank);
int flux_recover_all (flux_t h);

#endif /* !FLUX_LIVE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

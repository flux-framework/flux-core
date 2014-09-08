#ifndef _FLUX_REPARENT_H
#define _FLUX_REPARENT_H

json_object *flux_lspeer (flux_t h, int rank);

int flux_reparent (flux_t h, int rank, const char *uri);

#endif /* !_FLUX_REPARENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

#ifndef _BROKER_MODHANDLE_H
#define _BROKER_MODHANDLE_H

flux_t modhandle_create (void *sock, const char *uuid,
                         uint32_t rank, zctx_t *zctx);

#endif /* !_BROKER_MODHANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

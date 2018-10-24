#ifndef _FLUX_CORE_SERVICE_H
#define _FLUX_CORE_SERVICE_H

#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 
 *  Register service `name` with the broker for this handle. On success
 *   request messages sent to "name.*" will be routed to this handle 
 *   until `flux_service_remove()` is called for `name`, or upon
 *   disconnect. 
 *
 *  On success, the returned future will be fulfilled with no error, o/w
 *   the future is fulfilled with errnum set appropriately:
 *
 *   EINVAL - Invalid service name
 *   EEXIST - Service already registered under this name
 *   ENOENT - Unable to lookup route to requesting sender
 * 
 */
flux_future_t *flux_service_register (flux_t *h, const char *name);

/*
 *  Unregister a previously registered service `name` for this handle.
 *
 *  On success, the returned future is fulfilled with no error, o/w
 *   the future is fulfilled with errnum set appropriately:
 *
 *  ENOENT - No such service registered as `name`
 *  EINVAL - Sender does not match current owner of service
 * 
 */  
flux_future_t *flux_service_unregister (flux_t *h, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_SERVICE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

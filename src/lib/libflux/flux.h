#ifndef _FLUX_H
#define _FLUX_H

typedef struct flux_handle_struct *flux_t;

#include "reactor.h"
#include "security.h"
#include "reduce.h"
#include "message.h"
#include "handle.h"
#include "panic.h"
#include "event.h"
#include "request.h"
#include "module.h"

#include "api.h"
#include "kvs.h"
#include "live.h"
#include "modctl.h"

/* Get information about this cmbd instance's position in the flux comms
 * session.
 */
int flux_info (flux_t h, int *rankp, int *sizep, bool *treerootp);
int flux_rank (flux_t h);
int flux_size (flux_t h);
bool flux_treeroot (flux_t h);

json_object *flux_lspeer (flux_t h, int rank);

/* Interface for logging via the comms' reduction network.
 */
void flux_log_set_facility (flux_t h, const char *facility);
int flux_vlog (flux_t h, int lev, const char *fmt, va_list ap);
int flux_log (flux_t h, int lev, const char *fmt, ...)
            __attribute__ ((format (printf, 3, 4)));

/* flux_getattr is used to read misc. attributes internal to the cmbd.
 * The caller must dispose of the returned string with free ().
 * The following attributes are valid:
 *    cmbd-snoop-uri   The name of the socket to be used by flux-snoop.
 *    cmbd-parent-uri  The name of parent socket.
 *    cmbd-request-uri The name of request socket.
 */
char *flux_getattr (flux_t h, int rank, const char *name);

/* Reparenting functions.
 */
int flux_reparent (flux_t h, int rank, const char *uri);

#endif /* !_FLUX_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

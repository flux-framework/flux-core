#ifndef FLUX_MRPC_H
#define FLUX_MRPC_H

#include <flux/core.h>

typedef struct flux_mrpc_struct flux_mrpc_t;

/* Group RPC
 *
 * Client:                          Servers:
 *   flux_mrpc_create()               flux_event_subscribe ("mrpc...")
 *   flux_mrpc_put_inarg()            while (true) {
 *   flux_mrpc() ------------------->   (receive event)
 *                                      flux_mrpc_create_fromevent()
 *                                      flux_mrpc_get_inarg()
 *                                      (do some work)
 *                                      flux_mrpc_put_outarg()
 *   (returns) <----------------------- flux_mrpc_respond()
 * - flux_mrpc_get_outarg() ...         flux_mrpc_destroy()
 * - flux_mrpc_destroy()              }
 */

flux_mrpc_t *flux_mrpc_create (flux_t h, const char *nodelist);
void flux_mrpc_destroy (flux_mrpc_t *mrpc);

int flux_mrpc_put_inarg (flux_mrpc_t *mrpc, const char *json_str);

int flux_mrpc_get_inarg (flux_mrpc_t *mrpc, char **json_str);

int flux_mrpc_put_outarg (flux_mrpc_t *mrpc, const char *json_str);

int flux_mrpc_get_outarg (flux_mrpc_t *mrpc, int nodeid, char **json_str);

/* returns nodeid (-1 at end)  */
int flux_mrpc_next_outarg (flux_mrpc_t *mrpc);
void flux_mrpc_rewind_outarg (flux_mrpc_t *mrpc);

int flux_mrpc (flux_mrpc_t *mrpc, const char *fmt, ...);

/* returns NULL, errno == EINVAL if not addressed to me */
flux_mrpc_t *flux_mrpc_create_fromevent (flux_t h, const char *json_str);

int flux_mrpc_respond (flux_mrpc_t *mrpc);

#endif /* !FLUX_MRPC_H */

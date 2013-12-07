#ifndef FLUX_MRPC_H
#define FLUX_MRPC_H

typedef struct flux_mrpc_struct *flux_mrpc_t;

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

flux_mrpc_t flux_mrpc_create (flux_t h, const char *nodelist);
void flux_mrpc_destroy (flux_mrpc_t f);

void flux_mrpc_put_inarg (flux_mrpc_t f, json_object *val);
int flux_mrpc_get_inarg (flux_mrpc_t f, json_object **valp);

void flux_mrpc_put_outarg (flux_mrpc_t f, json_object *val);
int flux_mrpc_get_outarg (flux_mrpc_t f, int nodeid, json_object **valp);

/* returns nodeid (-1 at end)  */
int flux_mrpc_next_outarg (flux_mrpc_t f);
void flux_mrpc_rewind_outarg (flux_mrpc_t f);

int flux_mrpc (flux_mrpc_t f, const char *fmt, ...);

/* returns NULL, errno == EINVAL if not addressed to me */
flux_mrpc_t flux_mrpc_create_fromevent (flux_t h, json_object *request);
int flux_mrpc_respond (flux_mrpc_t f);

#endif /* !FLUX_MRPC_H */

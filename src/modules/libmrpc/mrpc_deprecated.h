#ifndef FLUX_MRPC_DEPRECATED_H
#define FLUX_MRPC_DEPRECATED_H

#include "mrpc.h"
#include <json.h>

void flux_mrpc_put_inarg_obj (flux_mrpc_t *mrpc, json_object *val)
	                      __attribute__ ((deprecated));
int flux_mrpc_get_inarg_obj (flux_mrpc_t *mrpc, json_object **valp)
	                     __attribute__ ((deprecated));

void flux_mrpc_put_outarg_obj (flux_mrpc_t *mrpc, json_object *val)
	                       __attribute__ ((deprecated));
int flux_mrpc_get_outarg_obj (flux_mrpc_t *mrpc, int nodeid, json_object **val)
	                      __attribute__ ((deprecated));

flux_mrpc_t *flux_mrpc_create_fromevent_obj (flux_t h, json_object *request)
	                                    __attribute__ ((deprecated));

#endif /* !FLUX_MRPC_DEPRECATED_H */

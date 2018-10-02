#ifndef _FLUX_KVS_MSG_CB_HANDLER_H
#define _FLUX_KVS_MSG_CB_HANDLER_H

#include <stdbool.h>
#include <flux/core.h>

/* A common set of code for managing message callbacks */

typedef struct msg_cb_handler msg_cb_handler_t;

msg_cb_handler_t *msg_cb_handler_create (flux_t *h, flux_msg_handler_t *w,
                                         const flux_msg_t *msg, void *arg,
                                         flux_msg_handler_f cb);

void msg_cb_handler_destroy (msg_cb_handler_t *mcb);

/* Set/get auxiliary data in the flux message stored in a msg_cb_handler_t */
int msg_cb_handler_msg_aux_set (msg_cb_handler_t *mcb, const char *name,
                                void *aux, flux_free_f destroy);
void *msg_cb_handler_msg_aux_get (msg_cb_handler_t *mcb, const char *name);

void msg_cb_handler_call (msg_cb_handler_t *mcb);

const flux_msg_t *msg_cb_handler_get_msgcopy (msg_cb_handler_t *mcb);

/* set a new callback handler */
void msg_cb_handler_set_cb (msg_cb_handler_t *mcb, flux_msg_handler_f cb);

#endif /* !_FLUX_KVS_MSG_CB_HANDLER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */


#ifndef _FLUX_JOB_MANAGER_PRIORITY_H
#define _FLUX_JOB_MANAGER_PRIORITY_H

#include "queue.h"

/* Handle a 'priority' request - job priority adjustment
 */
void priority_handle_request (flux_t *h, struct queue *queue,
                           const flux_msg_t *msg);


#endif /* ! _FLUX_JOB_MANAGER_PRIORITY_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

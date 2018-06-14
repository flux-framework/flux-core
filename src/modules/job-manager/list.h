#ifndef _FLUX_JOB_MANAGER_LIST_H
#define _FLUX_JOB_MANAGER_LIST_H

#include <jansson.h>
#include "queue.h"

/* Handle a 'list' request - to list the queue.
 */
void list_handle_request (flux_t *h, struct queue *queue,
                          const flux_msg_t *msg);

/* exposed for unit testing only */
json_t *list_one_job (struct job *job, json_t *attrs);
json_t *list_job_array (struct queue *queue, int max_entries, json_t *attrs);

#endif /* ! _FLUX_JOB_MANAGER_LIST_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

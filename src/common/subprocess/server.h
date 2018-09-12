#ifndef _SUBPROCESS_SERVER_H
#define _SUBPROCESS_SERVER_H

#include "subprocess.h"

int server_start (flux_subprocess_server_t *s, const char *prefix);

void server_stop (flux_subprocess_server_t *s);

int server_terminate_by_uuid (flux_subprocess_server_t *s,
                              const char *id);

#endif /* !_SUBPROCESS_SERVER_H */

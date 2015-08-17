#ifndef _FLUX_CORE_HEARTBEAT
#define _FLUX_CORE_HEARTBEAT

#include "message.h"

flux_msg_t *flux_heartbeat_encode (int epoch);
int flux_heartbeat_decode (const flux_msg_t *msg, int *epoch);

#endif /* !_FLUX_CORE_HEARTBEAT */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */


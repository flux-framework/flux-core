#ifndef _FLUX_CORE_HEARTBEAT
#define _FLUX_CORE_HEARTBEAT

#include "message.h"

#ifdef __cplusplus
extern "C" {
#endif

flux_msg_t *flux_heartbeat_encode (int epoch);
int flux_heartbeat_decode (const flux_msg_t *msg, int *epoch);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_HEARTBEAT */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */


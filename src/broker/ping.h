#ifndef BROKER_PING_H
#define BROKER_PING_H

#include <flux/core.h>

int ping_initialize (flux_t *h, const char *service);

#endif /* BROKER_PING_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

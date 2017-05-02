#ifndef BROKER_NAMESPACE_H
#define BROKER_NAMESPACE_H

#include <flux/core.h>

/* flags */
enum {
    FLUX_NS_SEQUENCED = 1,      // require sequenced updates
    FLUX_NS_SYNCHRONIZE = 2,    // publish ns.sync on create/remove/update
};

int namespace_initialize (flux_t *h);

#endif /* BROKER_NAMESPACE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

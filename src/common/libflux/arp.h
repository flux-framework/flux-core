#ifndef __FLUX_CORE_ARP_H
#define __FLUX_CORE_ARP_H

#include "fop.h"

#define ARP_AUTORELEASE_FOR                                         \
    for (ssize_t i = 0, __arp_for_scope = arp_scope_push (); i < 1; \
         ++i, arp_scope_pop (__arp_for_scope))

typedef struct arp_pair arp_t;

ssize_t arp_scope_push ();
ssize_t arp_scope_pop (ssize_t scope);
ssize_t arp_scope_pop_one ();

typedef void (*arp_cb_f) (void *);

fop *arp_autorelease (fop *o);
void *arp_auto_call (void *o, arp_cb_f fn);

#endif /* __FLUX_CORE_ARP_H */

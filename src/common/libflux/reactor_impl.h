#ifndef _FLUX_CORE_REACTOR_IMPL_H
#define _FLUX_CORE_REACTOR_IMPL_H

#include  "handle_impl.h"

struct reactor *reactor_get (flux_t h);

struct reactor *reactor_create (void *impl, const struct flux_handle_ops *ops);
void reactor_destroy (struct reactor *r);

#endif /* _FLUX_CORE_REACTOR_IMPL_H */

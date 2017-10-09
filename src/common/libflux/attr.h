#ifndef _FLUX_CORE_ATTR_H
#define _FLUX_CORE_ATTR_H

#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Flags can only be set by the broker.
 */
enum {
    FLUX_ATTRFLAG_IMMUTABLE = 1,    /* attribute is cacheable */
    FLUX_ATTRFLAG_READONLY = 2,     /* attribute cannot be written */
                                    /*   but may change on broker */
    FLUX_ATTRFLAG_ACTIVE = 4,       /* attribute has get and/or set callbacks */
};

const char *flux_attr_get (flux_t *h, const char *name, int *flags);

int flux_attr_set (flux_t *h, const char *name, const char *val);

int flux_attr_fake (flux_t *h, const char *name, const char *val, int flags);

const char *flux_attr_first (flux_t *h);

const char *flux_attr_next (flux_t *h);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_ATTR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

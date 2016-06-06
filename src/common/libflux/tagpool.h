#ifndef _FLUX_CORE_TAGPOOL_H
#define _FLUX_CORE_TAGPOOL_H

#include <stdint.h>

enum {
    TAGPOOL_FLAG_GROUP = 1,
};

struct tagpool *tagpool_create (void);
void tagpool_destroy (struct tagpool *t);
uint32_t tagpool_alloc (struct tagpool *t, int flags);
void tagpool_free (struct tagpool *t, uint32_t matchtag);

enum {
    TAGPOOL_ATTR_REGULAR_SIZE,
    TAGPOOL_ATTR_REGULAR_AVAIL,
    TAGPOOL_ATTR_GROUP_SIZE,
    TAGPOOL_ATTR_GROUP_AVAIL,
};
uint32_t tagpool_getattr (struct tagpool *t, int attr);


#endif /* _FLUX_CORE_TAGPOOL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

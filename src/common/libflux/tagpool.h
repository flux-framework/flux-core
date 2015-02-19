#ifndef _FLUX_CORE_TAGPOOL_H
#define _FLUX_CORE_TAGPOOL_H

#include <stdint.h>

typedef struct tagpool_struct *tagpool_t;

tagpool_t tagpool_create (void);
void tagpool_destroy (tagpool_t t);
uint32_t tagpool_alloc (tagpool_t t, int len);
void tagpool_free (tagpool_t t, uint32_t matchtag, int len);
uint32_t tagpool_avail (tagpool_t t);

enum {
    TAGPOOL_ATTR_BLOCKS,
    TAGPOOL_ATTR_BLOCKSIZE,
    TAGPOOL_ATTR_SSIZE,
};
uint32_t tagpool_getattr (tagpool_t, int attr);


#endif /* _FLUX_CORE_TAGPOOL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

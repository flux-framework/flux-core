/* nodeset_t - set of unsigned integer ranks */

#ifndef _UTIL_NODESET_H
#define _UTIL_NODESET_H

#include <stdint.h>
#include <stdbool.h>

typedef struct nodeset_struct nodeset_t;
typedef struct nodeset_iterator_struct nodeset_iterator_t;

#define NODESET_EOF (~(uint32_t)0)

/* Create/destroy/dup a nodeset_t.
 */
void nodeset_destroy (nodeset_t *n);

nodeset_t *nodeset_create (void);
nodeset_t *nodeset_create_size (uint32_t size);
nodeset_t *nodeset_create_range (uint32_t a, uint32_t b);
nodeset_t *nodeset_create_rank (uint32_t r);
nodeset_t *nodeset_create_string (const char *s);

nodeset_t *nodeset_dup (nodeset_t *n);

/* Configure separator used in nodeset_string().
 * Default: ','
 */
void nodeset_config_separator (nodeset_t *n, char c);

/* Configure whether nodeset_string() will use hyphenated ranges.
 * Default: enabled
 */
void nodeset_config_ranges (nodeset_t *n, bool enable);

/* Configure whether nodeset_string() will use brackets to distinguish
 * a set of ranks from a single rank.
 * Default: enabled
 */
void nodeset_config_brackets (nodeset_t *n, bool enable);

/* Configure whether nodeset_string() will pad with leading zeroes (max 10).
 * Default: disabled (pad = 0)
 */
void nodeset_config_padding (nodeset_t *n, unsigned pad);

/* Configure the internal size of the nodeset_t (capacity).
 * When shrinking, target size will be automatically increased to fit the
 * higest rank in the nodeset_t, and to be at least the minimum size (1K).
 * N.B. it is not necessary to make this call before adding a rank >= size.
 * When that occurs size will be increased automatically, but this call
 * will save time for expected set size >> default.
 * Default: 1K (internal size ~133 bytes)
 */
bool nodeset_resize (nodeset_t *n, uint32_t size);

/* Drop nodeset_string()'s cache and call nodeset_resize() with size = 0.
 */
void nodeset_minimize (nodeset_t *n);

/* Add range/rank/string-nodeset to nodeset_t.
 */
bool nodeset_add_range (nodeset_t *n, uint32_t a, uint32_t b);
bool nodeset_add_rank (nodeset_t *n, uint32_t r);
bool nodeset_add_string (nodeset_t *n, const char *s);

/* Delete range/rank/string-nodeset from nodeset_t.
 */
void nodeset_delete_range (nodeset_t *n, uint32_t a, uint32_t b);
void nodeset_delete_rank (nodeset_t *n, uint32_t r);
bool nodeset_delete_string (nodeset_t *n, const char *s);

/* Test (full) list membership of range/rank/string-nodeset_t.
 */
bool nodeset_test_range (nodeset_t *n, uint32_t a, uint32_t b);
bool nodeset_test_rank (nodeset_t *n, uint32_t r);
bool nodeset_test_string (nodeset_t *n, const char *s);

/* Get string-nodeset from nodeset.  Do not free.
 */
const char *nodeset_string (nodeset_t *n);

/* Return the number of nodes in the list.
 */
uint32_t nodeset_count (nodeset_t *n);

/* Return the min/max node ranks in the list, or NODESET_EOF if list is empty.
 */
uint32_t nodeset_min (nodeset_t *n);
uint32_t nodeset_max (nodeset_t *n);

/* Return next rank above r in the list, or NODESET_EOF if list is empty
 */
uint32_t nodeset_next_rank (nodeset_t *n, uint32_t r);

/* Iteration.  Terminate when NODESET_EOF is returned.
 */
nodeset_iterator_t *nodeset_iterator_create (nodeset_t *n);
void nodeset_iterator_destroy (nodeset_iterator_t *itr);
uint32_t nodeset_next (nodeset_iterator_t *itr);
void nodeset_iterator_rewind (nodeset_iterator_t *itr);

/* Query internal nodeset attributes (mainly for testing)
 */
enum {
    NODESET_ATTR_BYTES,     /* current internal size of nodeset in bytes */
    NODESET_ATTR_SIZE,      /* current veb set size */
    NODESET_ATTR_MINSIZE,   /* minimum veb set size (a constant) */
    NODESET_ATTR_MAXSIZE,   /* maximum possible veb set size (a constant) */
    NODESET_ATTR_MAXRANK,   /* maximum possible rank (a constant ) */
};
uint32_t nodeset_getattr (nodeset_t *n, int attr);

#endif /* !_UTIL_NODESET_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

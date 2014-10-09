/* nodeset_t - set of unsigned integer ranks */

#ifndef _UTIL_NODESET_H
#define _UTIL_NODESET_H

#include <stdint.h>
#include <stdbool.h>

typedef struct nodeset_struct *nodeset_t;
typedef struct nodeset_itr_struct *nodeset_itr_t;

#define NODESET_EOF (~(uint32_t)0)

/* Create/destroy/dup a nodeset_t.
 */
void nodeset_destroy (nodeset_t n);

nodeset_t nodeset_new (void);
nodeset_t nodeset_new_size (uint32_t size);
nodeset_t nodeset_new_range (uint32_t a, uint32_t b);
nodeset_t nodeset_new_rank (uint32_t r);
nodeset_t nodeset_new_str (const char *s);

nodeset_t nodeset_dup (nodeset_t n);

/* Configure separator used in nodeset_str().
 * Default: ','
 */
void nodeset_conf_separator (nodeset_t n, char c);

/* Configure whether nodeset_str() will use hyphenated ranges.
 * Default: enabled
 */
void nodeset_conf_ranges (nodeset_t n, bool enable);

/* Configure whether nodeset_str() will use brackets to distinguish
 * a set of ranks from a single rank.
 * Default: enabled
 */
void nodeset_conf_brackets (nodeset_t n, bool enable);

/* Configure whether nodeset_str() will pad with leading zeroes (max 10).
 * Default: disabled (pad = 0)
 */
void nodeset_conf_padding (nodeset_t n, unsigned pad);

/* Configure the internal size of the nodeset_t (capacity).
 * When shrinking, target size will be automatically increased to fit the
 * higest rank in the nodeset_t, and to be at least the minimum size (1K).
 * N.B. it is not necessary to make this call before adding a rank >= size.
 * When that occurs size will be increased automatically, but this call
 * will save time for expected set size >> default.
 * Default: 1K (internal size ~133 bytes)
 */
bool nodeset_resize (nodeset_t n, uint32_t size);

/* Drop nodeset_str()'s cache and call nodeset_resize() with size = 0.
 */
void nodeset_minimize (nodeset_t n);

/* Add range/rank/string-nodeset to nodeset_t.
 */
bool nodeset_add_range (nodeset_t n, uint32_t a, uint32_t b);
bool nodeset_add_rank (nodeset_t n, uint32_t r);
bool nodeset_add_str (nodeset_t n, const char *s);

/* Delete range/rank/string-nodeset from nodeset_t.
 */
void nodeset_del_range (nodeset_t n, uint32_t a, uint32_t b);
void nodeset_del_rank (nodeset_t n, uint32_t r);
bool nodeset_del_str (nodeset_t n, const char *s);

/* Test (full) list membership of range/rank/string-nodeset_t.
 */
bool nodeset_test_range (nodeset_t n, uint32_t a, uint32_t b);
bool nodeset_test_rank (nodeset_t n, uint32_t r);
bool nodeset_test_str (nodeset_t n, const char *s);

/* Get string-nodeset from nodeset.  Do not free.
 */
const char *nodeset_str (nodeset_t n);

/* Return the number of nodes in the list.
 */
uint32_t nodeset_count (nodeset_t n);

/* Return the min/max node ranks in the list, or NODESET_EOF if list is empty.
 */
uint32_t nodeset_min (nodeset_t n);
uint32_t nodeset_max (nodeset_t n);

/* Iteration.  Terminate when NODESET_EOF is returned.
 */
nodeset_itr_t nodeset_itr_new (nodeset_t n);
void nodeset_itr_destroy (nodeset_itr_t itr);
uint32_t nodeset_next (nodeset_itr_t itr);
void nodeset_itr_rewind (nodeset_itr_t itr);

#endif /* !_UTIL_NODESET_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

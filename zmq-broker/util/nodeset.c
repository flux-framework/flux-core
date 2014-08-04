/* nodeset.c - set of unsigned integer ranks */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "util.h"
#include "veb.h"
#include "nodeset.h"
#include "log.h"

static const int string_initsize = 4096;
static const uint32_t veb_minsize = 1<<10;

#undef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#undef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))

#define NS_MAGIC 0xfeef0001
struct nodeset_struct {
    int magic;
    int refcount;

    Veb T;      /* Van Emde Boas tree - T.D is data, T.M is size */
                /*  all ops are O(log m), for key bitsize m: 2^m = T.M */
    char conf_separator[2];
    bool conf_ranges;
    bool conf_brackets;
    unsigned int conf_padding;

    char *s;    /* cached string representation */
    int s_size;
    bool s_valid;
};

#define NS_ITR_MAGIC 0xf004feef
struct nodeset_itr_struct {
    int magic;
    nodeset_t n;
    uint32_t r;
    bool started;
};

static bool nodeset_single (nodeset_t n);

#define NS_FIRST(n)    vebsucc ((n)->T,0)
#define NS_NEXT(n,r)   vebsucc ((n)->T,(r)+1)
#define NS_LAST(n)     vebpred ((n)->T,(n)->T.M-1)
#define NS_PREV(n,r)   vebpred ((n)->T,(r)-1)
#define NS_SIZE(n)     ((n)->T.M)
#define NS_TEST(n,r)   (vebsucc ((n)->T,(r))==(r))

#define ABS_MAX_SIZE   (~(uint32_t)0)
#define ABS_MAX_RANK   (~(uint32_t)0 - 1)

static Veb vebdup (Veb T)
{
    uint32_t size = vebsize (T.M);
    Veb t;

    t.k = T.k;
    t.M = T.M;
    if ((t.D = malloc (size)))
        memcpy (t.D, T.D, size);
    return t;
}

nodeset_t nodeset_new_size (uint32_t size)
{
    nodeset_t n = malloc (sizeof (*n));
    if (!n)
        oom ();
    n->magic = NS_MAGIC;
    n->refcount = 1;
    n->T = vebnew (size, 0);
    if (!n->T.D)
        oom ();
    n->conf_separator[0] = ',';
    n->conf_separator[1] = '\0';
    n->conf_ranges = true;
    n->conf_brackets = true;
    n->conf_padding = 0;
    n->s = NULL;
    n->s_size = 0;
    n->s_valid = false;
    return n;
}

nodeset_t nodeset_new (void)
{
    return nodeset_new_size (veb_minsize);
}

nodeset_t nodeset_dup (nodeset_t n)
{
    nodeset_t cpy = malloc (sizeof (*cpy));

    if (!cpy)
        oom ();
    memcpy (cpy, n, sizeof (*cpy));
    cpy->refcount = 1;
    cpy->T = vebdup (n->T);
    if (!cpy->T.D)
        oom ();
    if (cpy->s_size > 0) {
        if (!(cpy->s = malloc (cpy->s_size)))
            oom ();
        memcpy (cpy->s, n->s, cpy->s_size);
    }
    return cpy;
}

static void nodeset_incref (nodeset_t n)
{
    n->refcount++;
}

static void nodeset_decref (nodeset_t n)
{
    if (--n->refcount == 0) {
        n->magic = ~NS_MAGIC;
        free (n->T.D);
        if (n->s)
            free (n->s);
        free (n);
    }
}

void nodeset_destroy (nodeset_t n)
{
    assert (n->magic == NS_MAGIC);
    nodeset_decref (n);
}

nodeset_t nodeset_new_str (const char *s)
{
    nodeset_t n = nodeset_new ();
    if (!nodeset_add_str (n, s)) {
        nodeset_destroy (n);
        return NULL;
    }
    return n;
}

nodeset_t nodeset_new_range (uint32_t a, uint32_t b)
{
    nodeset_t n = nodeset_new ();
    nodeset_add_range (n, a, b);
    return n;
}

nodeset_t nodeset_new_rank (uint32_t r)
{
    nodeset_t n = nodeset_new ();
    nodeset_add_rank (n, r);
    return n;
}

void nodeset_conf_separator (nodeset_t n, char c)
{
    assert (n->magic == NS_MAGIC);
    if (n->conf_separator[0] != c)
        n->s_valid = false;
    n->conf_separator[0] = c;
}

void nodeset_conf_ranges (nodeset_t n, bool enable)
{
    assert (n->magic == NS_MAGIC);
    if (n->conf_ranges != enable)
        n->s_valid = false;
    n->conf_ranges = enable;
}

void nodeset_conf_brackets (nodeset_t n, bool enable)
{
    assert (n->magic == NS_MAGIC);
    if (n->conf_brackets != enable)
        n->s_valid = false;
    n->conf_brackets = enable;
}

void nodeset_conf_padding (nodeset_t n, unsigned int padding)
{
    assert (n->magic == NS_MAGIC);
    if (padding > 10)
        padding = 10;
    if (n->conf_padding != padding)
        n->s_valid = false;
    n->conf_padding = padding;
}

bool nodeset_resize (nodeset_t n, uint32_t size)
{
    assert (n->magic == NS_MAGIC);

    uint32_t r;
    Veb T;

    if (size < veb_minsize)         /* don't allow size below minimum */
        size = veb_minsize;
    if (size < NS_SIZE (n)) {       /* If shrinking, bump size up to */
        r = NS_FIRST (n);           /*   fit highest rank in set. */
        while (r < NS_SIZE (n)) {
            if (r >= size)
                size = r + 1;
            r = NS_NEXT (n, r);
        }
    }
    if (size != NS_SIZE (n)) {
        T = vebnew (size, 0);
        if (!T.D)
            oom ();
        r = NS_FIRST (n);
        while (r < NS_SIZE (n)) {
            vebput (T, r);
            r = NS_NEXT (n, r);
        }
        free (n->T.D);
        n->T = T;
    }
    return true;
}

#define CHECK_SIZE(o,n) ((n)>(o)&&(o)<=ABS_MAX_SIZE)

static bool nodeset_expandtofit (nodeset_t n, uint32_t r)
{
    uint32_t size = NS_SIZE (n);
    while (size <= r && CHECK_SIZE(size, size << 1))
        size = size << 1;
    while (size <= r && CHECK_SIZE(size, size + veb_minsize))
        size += veb_minsize;
    if (size <= r && CHECK_SIZE(size, r + 1))
        size = r + 1;
    if (size <= r)
        return false;
    return nodeset_resize (n, size);
}

void nodeset_minimize (nodeset_t n)
{
    assert (n->magic == NS_MAGIC);
    nodeset_resize (n, 0);
    if (n->s) {
        free (n->s);
        n->s = NULL;
        n->s_size = 0;
        n->s_valid = false;
    }
}

nodeset_itr_t nodeset_itr_new (nodeset_t n)
{
    nodeset_itr_t itr = malloc (sizeof (*itr));
    if (!itr)
        oom ();
    itr->magic = NS_ITR_MAGIC;
    itr->n = n;
    nodeset_incref (itr->n);
    itr->r = NODESET_EOF;
    itr->started = false;
    return itr;
}

void nodeset_itr_destroy (nodeset_itr_t itr)
{
    assert (itr->magic == NS_ITR_MAGIC);
    nodeset_decref (itr->n);
    itr->magic = ~NS_ITR_MAGIC;
    free (itr);
}

uint32_t nodeset_next (nodeset_itr_t itr)
{
    if (itr->started)
        itr->r = NS_NEXT (itr->n, itr->r);
    else {
        itr->r = NS_FIRST (itr->n);
        itr->started = true;
    }
    return (itr->r == NS_SIZE (itr->n) ? NODESET_EOF : itr->r);
}

void nodeset_itr_rewind (nodeset_itr_t itr)
{
    itr->started = false;
}

static uint32_t nodeset_bytes (nodeset_t n)
{
    assert (n->magic == NS_MAGIC);
    return vebsize (n->T.M) + n->s_size + sizeof (*n);
}

bool nodeset_add_rank (nodeset_t n, uint32_t r)
{
    assert (n->magic == NS_MAGIC);

    if (NS_SIZE (n) <= r && !nodeset_expandtofit (n, r))
        return false;
    vebput (n->T, r);
    n->s_valid = false;
    return true;
}

bool nodeset_add_range (nodeset_t n, uint32_t a, uint32_t b)
{
    assert (n->magic == NS_MAGIC);

    uint32_t r;
    uint32_t lo = MIN(a,b);
    uint32_t hi = MAX(a,b);

    if (NS_SIZE (n) <= hi && !nodeset_expandtofit (n, hi))
        return false;
    for (r = lo; r <= hi; r++)
        vebput (n->T, r);
    n->s_valid = false;
    return true;
}

void nodeset_del_rank (nodeset_t n, uint32_t r)
{
    assert (n->magic == NS_MAGIC);
    if (r <= ABS_MAX_RANK)
        vebdel (n->T, r);
    n->s_valid = false;
}

void nodeset_del_range (nodeset_t n, uint32_t a, uint32_t b)
{
    assert (n->magic == NS_MAGIC);

    uint32_t r;
    uint32_t lo = MIN(a,b);
    uint32_t hi = MAX(a,b);

    for (r = lo; r <= hi; r++)
        if (r <= ABS_MAX_RANK)
            vebdel (n->T, r);
    n->s_valid = false;
}

bool nodeset_test_rank (nodeset_t n, uint32_t r)
{
    assert (n->magic == NS_MAGIC);
    return r <= ABS_MAX_RANK ? NS_TEST (n, r) : false;
}

bool nodeset_test_range (nodeset_t n, uint32_t a, uint32_t b)
{
    assert (n->magic == NS_MAGIC);

    uint32_t r;
    uint32_t lo = MIN(a,b);
    uint32_t hi = MAX(a,b);

    if (hi > ABS_MAX_RANK)
        return false;
    for (r = lo; r <= hi; r++)
        if (!NS_TEST (n, r))
            return false;
    return true;
}

static void
catstr (char **sp, int *lenp, int *usedp, const char *adds)
{
    int l = strlen (adds);
    int buflen = *lenp;

    while (*usedp + l + 1 > buflen)
        buflen *= 2;
    if (buflen > *lenp) {
        *lenp = buflen;
        if (!(*sp = realloc (*sp, *lenp)))
            oom ();
    }
    strcpy (*sp + *usedp, adds);
    *usedp += l;
}

const char *nodeset_str (nodeset_t n)
{
    assert (n->magic == NS_MAGIC);

    const char *sep = "";
    int used = 0;
    uint32_t r, lo, hi;
    bool inrange = false;
    char tmp[128];

    if (!n->s_valid) {
        if (!n->s) {
            n->s_size = string_initsize;
            if (!(n->s = malloc (n->s_size)))
                oom ();
        }
        n->s[0] = '\0';
        if (n->conf_brackets && !nodeset_single (n))
            catstr (&n->s, &n->s_size, &used, "[");
        r = NS_FIRST (n);
        while (r < NS_SIZE (n) || inrange) {
            if (n->conf_ranges) {
                if (!inrange) {
                    lo = hi = r;
                    inrange = true;
                } else if (r < NS_SIZE (n) && r == hi + 1) {
                    hi++;
                } else if (lo == hi) {
                    snprintf (tmp, sizeof (tmp), "%s%0*u", sep,
                              n->conf_padding, lo);
                    catstr (&n->s, &n->s_size, &used, tmp);
                    sep = n->conf_separator;
                    inrange = false;
                    continue;
                } else {
                    snprintf (tmp, sizeof (tmp), "%s%0*u-%0*u", sep,
                             n->conf_padding, lo,
                             n->conf_padding, hi);
                    catstr (&n->s, &n->s_size, &used, tmp);
                    sep = n->conf_separator;
                    inrange = false;
                    continue;
                }
            } else {
                snprintf (tmp, sizeof (tmp), "%s%0*u", sep,
                          n->conf_padding, r);
                catstr (&n->s, &n->s_size, &used, tmp);
                sep = n->conf_separator;
                inrange = false;
            }
            if (r < NS_SIZE (n))
                r = NS_NEXT (n, r);
        }
        if (n->s[0] == '[')
            catstr (&n->s, &n->s_size, &used, "]");
        n->s_valid = true;
    }
    return n->s;
}

static bool str2rank (const char *s, uint32_t *rp)
{
    char *endptr = NULL;
    uint32_t r = strtoul (s, &endptr, 10);

    if (endptr == s || *endptr != '\0')
        return false;
    *rp = r;
    return true;
}

typedef enum { OP_ADD, OP_DEL, OP_TEST } op_t;

static bool nodeset_op_str (nodeset_t n, op_t op, const char *str)
{
    char *cpy;
    int len;
    char *p, *s, *saveptr, *a1;
    uint32_t lo, hi;
    int count = 0;

    len = strlen (str);
    if (str[0] == '[' && str[len - 1] == ']') { /* hostlist compat */
        if (!(cpy = strdup (str + 1)))
            oom ();
        cpy[len - 2] = '\0';
    } else
        if (!(cpy = strdup (str)))
            oom ();

    a1 = cpy;
    while ((s = strtok_r (a1, ",", &saveptr))) {
        if ((p = strchr (s, '-'))) {
            *p = '\0';
            if (!str2rank (s, &lo) || !str2rank (p + 1, &hi))
                break;
            if (op == OP_DEL) {
                nodeset_del_range (n, hi, lo);
            } else if (op == OP_ADD) {
                if (!nodeset_add_range (n, hi, lo))
                    break;
            } else if (op == OP_TEST) {
                if (!nodeset_test_range (n, hi, lo))
                    break;
            }
        } else {
            if (!str2rank (s, &lo))
                break;
            if (op == OP_DEL) {
                nodeset_del_rank (n, lo);
            } else if (op == OP_ADD) {
                if (!nodeset_add_rank (n, lo))
                    break;
            } else if (op == OP_TEST) {
                if (!nodeset_test_rank (n, lo))
                    break;
            }
        }
        a1 = NULL;
        count++;
    }
    free (cpy);
    if (s || (count == 0 && strlen (str) > 0))
        return false;
    return true;
}

bool nodeset_add_str (nodeset_t n, const char *str)
{
    assert (n->magic == NS_MAGIC);
    return nodeset_op_str (n, OP_ADD, str);
}

bool nodeset_del_str (nodeset_t n, const char *str)
{
    assert (n->magic == NS_MAGIC);
    return nodeset_op_str (n, OP_DEL, str);
}

bool nodeset_test_str (nodeset_t n, const char *str)
{
    assert (n->magic == NS_MAGIC);
    return nodeset_op_str (n, OP_TEST, str);
}

uint32_t nodeset_count (nodeset_t n)
{
    assert (n->magic == NS_MAGIC);

    uint32_t count = 0;
    uint32_t r;

    r = NS_FIRST (n);
    while (r < NS_SIZE (n)) {
        count++;
        r = NS_NEXT (n, r);
    }
    return count;
}

uint32_t nodeset_min (nodeset_t n)
{
    assert (n->magic == NS_MAGIC);
    uint32_t r = NS_FIRST (n);
    return r == NS_SIZE (n) ? NODESET_EOF : r;
}

uint32_t nodeset_max (nodeset_t n)
{
    assert (n->magic == NS_MAGIC);
    uint32_t r = NS_LAST (n);
    return r == NS_SIZE (n) ? NODESET_EOF : r;
}

static bool nodeset_single (nodeset_t n)
{
    uint32_t len = 0;
    uint32_t r;

    r = NS_FIRST (n);
    while (r < NS_SIZE (n) && len < 2) {
        len++;
        r = NS_NEXT (n, r);
    }
    return (len < 2);
}

void nodeset_selftest (void)
{
    nodeset_t n, n2;
    nodeset_itr_t itr;
    int i;
    struct timespec ts;
    uint32_t r, bigset = 1E6;
    char *tmp;

    n = nodeset_new ();
    assert (n != NULL);
    nodeset_conf_brackets (n, false);

    /* Add three consecutive ranks.
     * 1 list entry, 3 ranks
     */
    nodeset_add_rank (n, 8);
    nodeset_add_rank (n, 7); /* add before */
    nodeset_add_rank (n, 9); /* add after */
    assert (!strcmp (nodeset_str (n), "7-9"));
    assert (nodeset_count (n) == 3);

    /* Add one disjoint rank before.
     *  2 list entries, 4 ranks
     */
    nodeset_add_rank (n, 1);
    assert (!strcmp (nodeset_str (n), "1,7-9"));
    assert (nodeset_count (n) == 4);

    /* Add one disjoint rank after.
     * 3 list entries, 5 ranks
     */
    nodeset_add_rank (n, 16);
    assert (!strcmp (nodeset_str (n), "1,7-9,16"));
    assert (nodeset_count (n) == 5);

    /* Insert one disjoint rank in the middle.
     * 4 list entries, 6 ranks
     */
    nodeset_add_rank (n, 14);
    assert (!strcmp (nodeset_str (n), "1,7-9,14,16"));
    assert (nodeset_count (n) == 6);

    /* Insert one disjoint rank in the middle.
     * 5 list entries, 7 ranks
     */
    nodeset_add_rank (n, 3);
    assert (!strcmp (nodeset_str (n), "1,3,7-9,14,16"));
    assert (nodeset_count (n) == 7);

    /* Insert range that contains two singletons.
     * 4 list entries, 8 ranks
     */
    nodeset_add_range (n, 1, 3);
    assert (!strcmp (nodeset_str (n), "1-3,7-9,14,16"));
    assert (nodeset_count (n) == 8);

    /* Insert range overlapping range on low side
     * 4 list entries, 10 ranks
     */
    nodeset_add_range (n, 5, 8);
    assert (!strcmp (nodeset_str (n), "1-3,5-9,14,16"));
    assert (nodeset_count (n) == 10);

    /* Insert range overlapping range on high side
     * 4 list entries, 12 ranks
     */
    nodeset_add_range (n, 8, 11);
    assert (!strcmp (nodeset_str (n), "1-3,5-11,14,16"));
    assert (nodeset_count (n) == 12);

    /* Add range that contains several singletons and ranges.
     * 1 list entries, 16 ranks
     */
    nodeset_add_range (n, 1, 16);
    assert (!strcmp (nodeset_str (n), "1-16"));
    assert (nodeset_count (n) == 16);

    /* Add a range contained in existing range.
     * No change.
     */
    nodeset_add_range (n, 4, 8);
    assert (!strcmp (nodeset_str (n), "1-16"));
    assert (nodeset_count (n) == 16);

    nodeset_destroy (n);

/********************************************/

    /* Add 0,1,2 to see if 1 is mergeable with 0 (edge case).
     */
    n = nodeset_new ();
    nodeset_add_rank (n, 0);
    nodeset_add_rank (n, 1);
    nodeset_add_rank (n, 2);
    assert (!strcmp (nodeset_str (n), "[0-2]"));
    assert (nodeset_count (n) == 3);
    nodeset_conf_ranges (n, false);
    assert (!strcmp (nodeset_str (n), "[0,1,2]"));
    nodeset_destroy (n);

/********************************************/

    /* Add 2,1,0 to test the reverse edge case
     */
    n = nodeset_new ();
    nodeset_add_rank (n, 2);
    nodeset_add_rank (n, 1);
    nodeset_add_rank (n, 0);
    assert (!strcmp (nodeset_str (n), "[0-2]"));
    assert (nodeset_count (n) == 3);
    nodeset_destroy (n);

/********************************************/

    /* Exercise nodeset_new_str.
     */

    n = nodeset_new_str ("[1,3,5,6-100]");
    assert (n != NULL);
    assert (!strcmp (nodeset_str (n), "[1,3,5-100]"));
    assert (nodeset_count (n) == 98);
    nodeset_destroy (n);

    n = nodeset_new_str ("2-1");
    assert (n != NULL);
    assert (!strcmp (nodeset_str (n), "[1-2]"));
    assert (nodeset_count (n) == 2);
    nodeset_destroy (n);

    n = nodeset_new_str ("");
    assert (n != NULL);
    assert (nodeset_count (n) == 0);
    assert (!strcmp (nodeset_str (n), ""));
    nodeset_destroy (n);

    n = nodeset_new_str (",");
    assert (n == NULL);

    n = nodeset_new_str ("-1");
    assert (n == NULL);

    n = nodeset_new_str ("1-");
    assert (n == NULL);

    n = nodeset_new_str ("foo1");
    assert (n == NULL);

    n = nodeset_new_str ("[1-2]");
    assert (n != NULL);
    assert (nodeset_count (n) == 2);
    nodeset_destroy (n);

    n = nodeset_new_str ("xyz");
    assert (n == NULL);

/********************************************/

    /* Exercise nodeset_del() / nodeset_test()
     */
    n = nodeset_new_str ("0-2");
    assert (n != NULL);
    assert (!strcmp (nodeset_str (n), "[0-2]"));
    assert (nodeset_test_range (n, 0, 2));
    nodeset_del_rank (n, 0);
    assert (!nodeset_test_rank (n, 0));
    assert (nodeset_test_range (n, 1, 2));
    assert (!strcmp (nodeset_str (n), "[1-2]"));
    nodeset_del_rank (n, 1);
    assert (!nodeset_test_rank (n, 0));
    assert (!nodeset_test_rank (n, 1));
    assert (nodeset_test_rank (n, 2));
    assert (!strcmp (nodeset_str (n), "2"));
    nodeset_del_rank (n, 2);
    assert (!n->s_valid);
    assert (!nodeset_test_rank (n, 0));
    assert (!nodeset_test_rank (n, 1));
    assert (!nodeset_test_rank (n, 2));
    assert (!strcmp (nodeset_str (n), ""));
    nodeset_destroy (n);

/********************************************/

    /* Exercise iteration
     */
    n = nodeset_new_str ("0-2");
    assert (n != NULL);
    itr = nodeset_itr_new (n);
    assert (nodeset_next (itr) == 0);
    assert (nodeset_next (itr) == 1);
    assert (nodeset_next (itr) == 2);
    assert (nodeset_next (itr) == NODESET_EOF);
    nodeset_itr_rewind (itr);
    assert (nodeset_next (itr) == 0);
    nodeset_itr_destroy (itr);
    nodeset_destroy (n);

/********************************************/

    /* Exercise nodeset_dup
     */
    n = nodeset_new_str ("0-2");
    assert (n != NULL);
    assert (!strcmp (nodeset_str (n), "[0-2]"));
    n2 = nodeset_dup (n);
    assert (n2 != NULL);
    assert (!strcmp (nodeset_str (n2), "[0-2]"));
    nodeset_add_rank (n, 4);
    nodeset_add_rank (n2, 5);
    assert (!strcmp (nodeset_str (n), "[0-2,4]"));
    assert (!strcmp (nodeset_str (n2), "[0-2,5]"));
    nodeset_destroy (n);
    nodeset_destroy (n2);

/********************************************/

    /* Try zero padding.
     */
    n = nodeset_new_str ("[1,3,5,6-100]");
    assert (!strcmp (nodeset_str (n), "[1,3,5-100]"));
    //nodeset_conf_padding (n, log10 (nodeset_max (n)) + 1);
    nodeset_conf_padding (n, 3);
    assert (!strcmp (nodeset_str (n), "[001,003,005-100]"));
    nodeset_conf_padding (n, 2);
    assert (!strcmp (nodeset_str (n), "[01,03,05-100]"));
    nodeset_conf_padding (n, 4);
    assert (!strcmp (nodeset_str (n), "[0001,0003,0005-0100]"));
    nodeset_destroy (n);

/********************************************/

    /* Add 'bigset' consecutive singletons.
     */
    monotime (&ts);
    n = nodeset_new ();
    nodeset_resize (n, bigset);
    for (i = 0; i < bigset; i++)
        nodeset_add_rank (n, i);
    msg ("add %u consecutive: %.2fs (%u Kbytes)", bigset,
         monotime_since (ts)/1000, nodeset_bytes (n)/1024);

    monotime (&ts);
    (void)nodeset_str (n); /* time the work */
    msg ("tostr %u consecutive: %.2fs (%u Kbytes)", bigset,
         monotime_since (ts)/1000, nodeset_bytes (n)/1024);

    if (asprintf (&tmp, "[0-%u]", bigset - 1) < 0)
        oom ();
    assert (!strcmp (nodeset_str (n), tmp));
    free (tmp);
    assert (nodeset_count (n) == bigset);

    nodeset_destroy (n);

/********************************************/

    /* Add 'bigset'/2 non-consecutive singletons.
     */
    monotime (&ts);
    n = nodeset_new ();
    nodeset_resize (n, bigset);
    for (i = 0; i < bigset; i += 2)
        nodeset_add_rank (n, i);
    msg ("add %u non-consecutive: %.2fs (%u Kbytes)", bigset/2,
         monotime_since (ts)/1000, nodeset_bytes (n)/1024);

    monotime (&ts);
    (void)nodeset_str (n); /* time the work */
    msg ("tostr %u non-consecutive: %.2fs (%u Kbytes)", bigset/2,
         monotime_since (ts)/1000, nodeset_bytes (n)/1024);

    assert (nodeset_count (n)  == bigset/2);

    nodeset_destroy (n);

/********************************************/

    /* Check edge cases with very big ranks and resize.
     */

    r = ABS_MAX_RANK;

    monotime (&ts);
    n = nodeset_new ();
    assert (NS_SIZE (n) == veb_minsize);

    assert (nodeset_add_rank (n, r + 1) == false);
    assert (nodeset_add_rank (n, r) == true);
    assert (NS_SIZE (n) == ABS_MAX_SIZE); // should have triggered expansion
    assert (nodeset_add_rank (n, r - 1) == true);
    msg ("set rank %u,%u,%u: %.2fs (%u Mbytes)", r-1, r, r+1,
         monotime_since (ts)/1000, nodeset_bytes (n)/(1024*1024));

    assert (nodeset_test_rank (n, r - 1));
    assert (nodeset_test_rank (n, r));
    assert (!nodeset_test_rank (n, r + 1));
    assert (nodeset_count (n) == 2);

    monotime (&ts);
    msg ("tostr %s: %.2fs (%u Mbytes)", nodeset_str (n),
         monotime_since (ts)/1000, nodeset_bytes (n)/(1024*1024));
    if (asprintf (&tmp, "[%u-%u]", r-1, r) < 0)
        oom ();
    assert (!strcmp (nodeset_str (n), tmp));
    free (tmp);

    assert (nodeset_resize (n, 0) == true); // should have no effect
    assert (NS_SIZE (n) == ABS_MAX_SIZE);

    nodeset_del_rank (n, r - 1);
    assert (!nodeset_test_rank (n, r - 1));
    assert (nodeset_test_rank (n, r));
    assert (!nodeset_test_rank (n, r + 1));

    nodeset_del_rank (n, r + 1);
    assert (!nodeset_test_rank (n, r - 1));
    assert (nodeset_test_rank (n, r));
    assert (!nodeset_test_rank (n, r + 1));

    nodeset_del_rank (n, r);
    assert (!nodeset_test_rank (n, r - 1));
    assert (!nodeset_test_rank (n, r));
    assert (!nodeset_test_rank (n, r + 1));

    assert (nodeset_resize (n, 0) == true); // this should shrink it
    assert (NS_SIZE (n) == veb_minsize);

    nodeset_destroy (n);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

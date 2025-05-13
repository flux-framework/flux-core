/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/idset.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/lru_cache.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"
#include "taskmap.h"

struct taskmap_block {
    int start;
    int nnodes;
    int ppn;
    int repeat;
};

struct taskmap {
    zlistx_t *blocklist;
    lru_cache_t *idsets;
};

static struct taskmap_block * taskmap_block_create (int nodeid,
                                                    int nnodes,
                                                    int ppn,
                                                    int repeat)
{
    struct taskmap_block *block = calloc (1, sizeof (*block));
    if (!block)
        return NULL;

    block->start = nodeid;
    block->nnodes = nnodes;
    block->ppn = ppn;
    block->repeat = repeat;
    return block;
}

static void taskmap_block_destroy (struct taskmap_block *block)
{
    if (block) {
        int saved_errno = errno;
        free (block);
        errno = saved_errno;
    }
}

static int taskmap_block_end (struct taskmap_block *block)
{
    return block->start + block->nnodes - 1;
}

static struct taskmap_block *taskmap_block_from_json (json_t *entry,
                                                      flux_error_t *errp)
{
    int nodeid;
    int nnodes;
    int ppn;
    int repeat;
    json_error_t error;
    struct taskmap_block *block;

    if (json_unpack_ex (entry, &error, JSON_DECODE_ANY,
                        "[iiii]",
                        &nodeid,
                        &nnodes,
                        &ppn,
                        &repeat) < 0) {
        errprintf (errp, "error in taskmap entry: %s", error.text);
        return NULL;
    }
    if (nodeid < 0 || nnodes <= 0 || ppn <= 0 || repeat <= 0) {
        errprintf (errp,
                   "invalid entry [%d,%d,%d,%d]",
                   nodeid,
                   nnodes,
                   ppn,
                   repeat);
        return NULL;
    }
    if (!(block = taskmap_block_create (nodeid, nnodes, ppn, repeat)))
        errprintf (errp, "Out of memory");
    return block;
}

static void taskmap_block_destructor (void **item)
{
    if (item) {
        taskmap_block_destroy (*item);
        *item = NULL;
    }
}

void taskmap_destroy (struct taskmap *map)
{
    if (map) {
        int saved_errno = errno;
        zlistx_destroy (&map->blocklist);
        lru_cache_destroy (map->idsets);
        free (map);
        errno = saved_errno;
    }
}

struct taskmap *taskmap_create (void)
{
    struct taskmap *map = NULL;

    if (!(map = calloc (1, sizeof (*map)))
        || !(map->blocklist = zlistx_new ())
        || !(map->idsets = lru_cache_create (16))) {
        errno = ENOMEM;
        goto error;
    }
    zlistx_set_destructor (map->blocklist, taskmap_block_destructor);
    lru_cache_set_free_f (map->idsets, (lru_cache_free_f) idset_destroy);
    return map;
error:
    taskmap_destroy (map);
    return NULL;
}

bool taskmap_unknown (const struct taskmap *map)
{
    /*  A zero-length mapping indicates that the task map is unknown */
    return zlistx_size (map->blocklist) == 0;
}

static char *to_string (int n, char *buf, int len)
{
    (void) snprintf (buf, len, "%d", n);
    return buf;
}

static void cache_idset (const struct taskmap *map,
                         int nodeid,
                         struct idset *idset)
{
    char id[24];
    (void) lru_cache_put (map->idsets,
                          to_string (nodeid, id, sizeof (id)),
                          idset);
}

static void decache_idset (struct taskmap *map, int nodeid)
{
    char id[24];
    (void) lru_cache_remove (map->idsets,
                             to_string (nodeid, id, sizeof (id)));
}

/*  Wrapper for zlistx_add_end that sets errno.
 *  Note: zlistx_add_end() asserts on failure, so there is currently no
 *  way this function can fail. Here we assume if in the future it does
 *  then ENOMEM is the likely cause.
 */
static int append_to_zlistx (zlistx_t *l, void *item)
{
    if (!zlistx_add_end (l, item)) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static void taskmap_find_repeats (struct taskmap *map)
{
    struct taskmap_block *block;
    struct taskmap_block *prev = NULL;

    prev = zlistx_first (map->blocklist);
    block = zlistx_next (map->blocklist);
    while (block) {
        if (block->start == prev->start
            && block->nnodes == prev->nnodes
            && block->ppn == prev->ppn) {
            prev->repeat += block->repeat;
            zlistx_delete (map->blocklist, zlistx_cursor (map->blocklist));
        }
        else
            prev = block;
        block = zlistx_next (map->blocklist);
    }
}

int taskmap_append (struct taskmap *map, int nodeid, int nnodes, int ppn)
{
    struct taskmap_block *block;

    if (!map || nodeid < 0 || nnodes <= 0 || ppn <= 0) {
        errno = EINVAL;
        return -1;
    }
    decache_idset (map, nodeid);
    if ((block = zlistx_tail (map->blocklist))) {
        /*  If previous block ends at nodeid - 1, and has the same ppn
         *  and a repeat of 1, then add nnodes to the previous block
         *  instead of appending a new block.
         */
        if (nodeid == taskmap_block_end (block) + 1
            && ppn == block->ppn
            && block->repeat == 1) {
            block->nnodes += nnodes;
            /*  Check for any new repeated blocks, then return
             */
            taskmap_find_repeats (map);
            return 0;
        }
        /*  If previous block and this block are a single, identical
         *  node, then increment block->ppn by ppn.
         */
        if (block->start == nodeid
            && block->nnodes == 1
            && nnodes == 1) {
            block->ppn += ppn;
            taskmap_find_repeats (map);
            return 0;
        }
        /*  O/w, if previous block matches (nodeid, nnodes, ppn), then
         *  increment the repeat of the previous block.
         */
        if (block->start == nodeid
            && block->nnodes == nnodes
            && block->ppn == ppn) {
            block->repeat++;
            return 0;
        }
        /* Else fall through and append a new block
         */
    }
    if (!(block = taskmap_block_create (nodeid, nnodes, ppn, 1))
        || append_to_zlistx (map->blocklist, block) < 0) {
        taskmap_block_destroy (block);
        return -1;
    }
    return 0;
}

static struct taskmap *taskmap_decode_array (json_t *o, flux_error_t *errp)
{
    size_t index;
    json_t *entry;
    struct taskmap *map = NULL;

    if (!json_is_array (o)) {
        errprintf (errp, "taskmap must be an array");
        goto err;
    }

    if (!(map = taskmap_create ())) {
        errprintf (errp, "Out of memory");
        goto err;
    }

    json_array_foreach (o, index, entry) {
        struct taskmap_block *block;
        if (!json_is_array (entry)) {
            errprintf (errp, "entry %zu in taskmap is not an array", index);
            goto err;
        }
        if (!(block = taskmap_block_from_json (entry, errp))
            || append_to_zlistx (map->blocklist, block) < 0)
            goto err;
    }
    return map;
err:
    taskmap_destroy (map);
    return NULL;
}

struct taskmap *taskmap_decode_json (json_t *o, flux_error_t *errp)
{
    struct taskmap *map = NULL;
    json_t *array;

    err_init (errp);

    if (!o) {
        errprintf (errp, "Invalid argument");
        goto error;
    }

    array = o;
    if (json_is_object (o)) {
        int version;
        json_error_t error;
        if (json_unpack_ex (o, &error, 0,
                            "{s:i s:o}",
                            "version", &version,
                            "map", &array) < 0) {
            errprintf (errp, "%s", error.text);
            goto error;
        }
        if (version != 1) {
            errprintf (errp, "expected version=1, got %d", version);
            goto error;
        }
    }
    else if (!json_is_array (o)) {
        errprintf (errp, "taskmap must be an object or array");
        goto error;
    }
    map = taskmap_decode_array (array, errp);
error:
    return map;
}

static int parse_pmi_block (const char *s, int *nodeid, int *count, int *ppn)
{
    char *endptr;
    errno = 0;
    *nodeid = strtoul (s, &endptr, 10);
    if (errno != 0 || *endptr != ',')
        return -1;
    s = endptr + 1;
    *count = strtoul (s, &endptr, 10);
    if (errno != 0 || *endptr != ',')
        return -1;
    s = endptr + 1;
    *ppn = strtoul (s, &endptr, 10);
    if (errno != 0 || *endptr != ')')
        return -1;
    return 0;
}

static bool is_empty (const char *s)
{
    while (isspace(*s))
        s++;
    return *s == '\0';
}

static struct taskmap *taskmap_decode_pmi (const char *s, flux_error_t *errp)
{
    char *tok;
    char *p;
    char *q;
    char *cpy = NULL;
    struct taskmap *map = NULL;
    bool got_sentinel = false;

    if (!s) {
        errprintf (errp, "Invalid argument");
        return NULL;
    }

    /* Empty PMI_process_mapping is allowed: return empty taskmap
     */
    if (strlen (s) == 0)
        return taskmap_create ();

    if (!(map = taskmap_create ())
        || !(cpy = strdup (s))) {
        errprintf (errp, "Out of memory");
        goto error;
    }

    p = cpy;
    while ((tok = strtok_r (p, "(", &q))) {
        int nodeid = -1;
        int count = -1;
        int ppn = -1;

        while (isspace (*tok))
            tok++;

        if (strstarts (tok, "vector,"))
            got_sentinel = true;
        else if (!is_empty (tok)) {
            if (!got_sentinel) {
                errprintf (errp, "vector prefix must precede blocklist");
                goto error;
            }
            if (parse_pmi_block (tok, &nodeid, &count, &ppn) < 0) {
                errprintf (errp, "unable to parse block: (%s", tok);
                goto error;
            }
            if (nodeid < 0 || count <= 0 || ppn <= 0) {
                errprintf (errp, "invalid number in block: (%s", tok);
                goto error;
            }
            if (taskmap_append (map, nodeid, count, ppn) < 0) {
                errprintf (errp, "taskmap_append: %s", strerror (errno));
                goto error;
            }
        }
        p = NULL;
    }
    if (taskmap_total_ntasks (map) == 0) {
        errprintf (errp, "no tasks found in PMI_process_mapping");
        goto error;
    }
    free (cpy);
    return map;
error:
    taskmap_destroy (map);
    free (cpy);
    return NULL;
}

struct raw_task {
    int taskid;
    int nodeid;
    int repeat;
};

static void item_destructor (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}

static int taskid_cmp (const void *a, const void *b)
{
    const struct raw_task *t1 = a;
    const struct raw_task *t2 = b;
    return (t1->taskid - t2->taskid);
}

static int raw_task_append (zlistx_t *l, int taskid, int nodeid, int repeat)
{
    struct raw_task *t = calloc (1, sizeof (*t));
    if (!t)
        return -1;
    t->taskid = taskid;
    t->nodeid = nodeid;
    t->repeat = repeat;
    if (!zlistx_add_end (l, t)) {
        free (t);
        return -1;
    }
    return 0;
}

static zlistx_t *raw_task_list_create (void)
{
    zlistx_t *l;
    if (!(l = zlistx_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    zlistx_set_destructor (l, item_destructor);
    zlistx_set_comparator (l, &taskid_cmp);
    return l;
}

static int raw_task_list_append (zlistx_t *l,
                                 const char *s,
                                 int nodeid,
                                 flux_error_t *errp)
{
    int rc = -1;
    unsigned int id;
    idset_error_t error;
    struct idset *ids;

    if (!(ids = idset_decode_ex (s, -1, 0, IDSET_FLAG_AUTOGROW, &error))) {
        errprintf (errp, "%s", error.text);
        goto error;
    }
    id = idset_first (ids);
    while (id != IDSET_INVALID_ID) {
        unsigned int next = idset_next (ids, id);
        int repeat = 1;
        while (next == id + repeat) {
            next = idset_next (ids, next);
            repeat++;
        }
        if (raw_task_append (l, id, nodeid, repeat) < 0) {
            errprintf (errp, "Out of memory");
            goto error;
        }
        id = next;
    }
    rc = 0;
error:
    idset_destroy (ids);
    return rc;
}

static int raw_task_check (struct raw_task *a,
                           struct raw_task *b,
                           flux_error_t *errp)
{
    struct raw_task t_init = { .taskid = -1, .repeat = 1 };
    int start, end1, end2, end;

    if (a == NULL)
        a = &t_init;

    /*  Note: a->taskid <= b->taskid since taskmap_decode_raw() sorts
     *   raw_task objects.
     */
    start = b->taskid;
    end1 = a->taskid + a->repeat - 1;
    end2 = b->taskid + b->repeat - 1;
    end = end1 <= end2 ? end1 : end2;

    /* If end - start is nonzero then we have overlap. report it.
     */
    int overlap = end - start;
    if (overlap >= 0) {
        /* taskid overlap detected, report as error
         */
        if (overlap == 0)
            errprintf (errp, "duplicate taskid specified: %d", start);
        else
            errprintf (errp, "duplicate taskids specified: %d-%d", start, end);
        return -1;
    }
    /*  Now check that tasks are consecutive. It is an error if not since
     *  holes in taskids in a taskmap are not allowed
     */
    if (overlap != -1) {
        if (overlap == -2)
            return errprintf (errp, "missing taskid: %d", end + 1);
        else
            return errprintf (errp,
                              "missing taskids: %d-%d",
                              end + 1,
                              end - overlap - 1);
    }
    return 0;
}

static struct taskmap *taskmap_decode_raw (const char *s, flux_error_t *errp)
{
    char *tok;
    char *p;
    char *q;
    char *cpy = NULL;
    struct taskmap *map = NULL;
    zlistx_t *l = NULL;
    int nodeid = 0;
    struct raw_task *t, *prev;

    if (!s || strlen (s) == 0) {
        errprintf (errp, "Invalid argument");
        return NULL;
    }
    if (!(map = taskmap_create ())
        || !(cpy = strdup (s))
        || !(l = raw_task_list_create ())) {
        errprintf (errp, "Out of memory");
        goto error;
    }

    p = cpy;

    while ((tok = strtok_r (p, ";", &q))) {
        if (raw_task_list_append (l, tok, nodeid++, errp) < 0)
            goto error;
        p = NULL;
    }

    /* sort by taskid */
    zlistx_sort (l);
    t = zlistx_first (l);
    prev = NULL;

    while (t) {
        if (raw_task_check (prev, t, errp) < 0)
            goto error;
        if (taskmap_append (map, t->nodeid, 1, t->repeat) < 0) {
            errprintf (errp, "taskmap_append: %s", strerror (errno));
            goto error;
        }
        prev = t;
        t = zlistx_next (l);
    }
    zlistx_destroy (&l);
    free (cpy);
    return map;
error:
    zlistx_destroy (&l);
    taskmap_destroy (map);
    free (cpy);
    return NULL;
}

struct taskmap *taskmap_decode (const char *s, flux_error_t *errp)
{
    struct taskmap *map = NULL;
    json_t *o = NULL;
    json_error_t error;

    err_init (errp);

    if (s == NULL) {
        errprintf (errp, "Invalid argument");
        goto out;
    }

    /*  Empty string or string containing "vector," may be a valid
     *  PMI_process_mapping. Pass to taskmap_decode_pmi().
     */
    if (strlen (s) == 0
        || strstr (s, "vector,"))
        return taskmap_decode_pmi (s, errp);

    /*  A string without special characters might be a raw taskmap:
     */
    if (!strpbrk (s, "({[]})"))
        return taskmap_decode_raw (s, errp);

    /*  O/w, decode as RFC 34 Taskmap
     */
    if (!(o = json_loads (s, JSON_DECODE_ANY, &error))) {
        errprintf (errp, "%s", error.text);
        goto out;
    }
    map = taskmap_decode_json (o, errp);
out:
    json_decref (o);
    return map;
}

static struct idset *lookup_idset (const struct taskmap *map, int nodeid)
{
    char id[24];
    return lru_cache_get (map->idsets, to_string (nodeid, id, sizeof (id)));
}

const struct idset *taskmap_taskids (const struct taskmap *map, int nodeid)
{
    int current;
    struct taskmap_block *block;
    struct idset *taskids;

    if (!map || nodeid < 0 || taskmap_unknown (map)) {
        errno = EINVAL;
        return NULL;
    }

    if ((taskids = lookup_idset (map, nodeid)))
        return taskids;

    if (!(taskids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;

    current = 0;
    block = zlistx_first (map->blocklist);
    while (block) {
        for (int n = 0; n < block->repeat; n++) {
            int start = block->start;
            if (nodeid >= start && nodeid <= taskmap_block_end (block)) {
                int offset = nodeid - start;
                start = current + (offset * block->ppn);
                idset_range_set (taskids, start, start + block->ppn - 1);
            }
            current += block->nnodes * block->ppn;
        }
        block = zlistx_next (map->blocklist);
    }

    if (idset_count (taskids) == 0) {
        idset_destroy (taskids);
        errno = ENOENT;
        return NULL;
    }

    cache_idset (map, nodeid, taskids);
    return taskids;
}

int taskmap_nodeid (const struct taskmap *map, int taskid)
{
    struct taskmap_block *block;
    int current = 0;

    if (!map || taskid < 0 || taskmap_unknown (map)) {
        errno = EINVAL;
        return -1;
    }

    block = zlistx_first (map->blocklist);
    while (block) {
        for (int n = 0; n < block->repeat; n++) {
            int last = current + block->nnodes * block->ppn - 1;
            if (taskid <= last) {
                int distance = taskid - current;
                return block->start + (distance / block->ppn);
            }
            current = last + 1;
        }
        block = zlistx_next (map->blocklist);
    }
    errno = ENOENT;
    return -1;
}

int taskmap_ntasks (const struct taskmap *map, int nodeid)
{
    const struct idset *taskids = taskmap_taskids (map, nodeid);
    if (!taskids)
        return -1;
    return idset_count (taskids);
}

int taskmap_nnodes (const struct taskmap *map)
{
    struct taskmap_block *block;
    int n;

    if (!map || taskmap_unknown (map)) {
        errno = EINVAL;
        return -1;
    }

    n = 0;
    block = zlistx_first (map->blocklist);
    while (block) {
        int end = block->start + block->nnodes;
        if (n < end)
            n = end;
        block = zlistx_next (map->blocklist);
    }
    return n;
}

int taskmap_total_ntasks (const struct taskmap *map)
{
    struct taskmap_block *block;
    int n;

    if (!map || taskmap_unknown (map)) {
        errno = EINVAL;
        return -1;
    }

    n = 0;
    block = zlistx_first (map->blocklist);
    while (block) {
        n += block->nnodes * block->repeat * block->ppn;
        block = zlistx_next (map->blocklist);
    }
    return n;
}

static json_t *taskmap_block_encode (struct taskmap_block *block)
{
    return json_pack ("[iiii]",
                      block->start,
                      block->nnodes,
                      block->ppn,
                      block->repeat);
}

json_t *taskmap_encode_json (const struct taskmap *map, int flags)
{
    struct taskmap_block *block;
    json_t *blocks = NULL;
    json_t *taskmap = NULL;

    if (!(blocks = json_array ()))
        goto error;

    block = zlistx_first (map->blocklist);
    while (block) {
        json_t *o = taskmap_block_encode (block);
        if (!o)
            goto error;
        if (json_array_append_new (blocks, o) < 0) {
            json_decref (o);
            goto error;
        }
        block = zlistx_next (map->blocklist);
    }
    if (!(flags & TASKMAP_ENCODE_WRAPPED))
        return blocks;
    if (!(taskmap = json_pack ("{s:i s:o}",
                               "version", 1,
                               "map", blocks)))
        goto error;
    return taskmap;
error:
    json_decref (blocks);
    json_decref (taskmap);

    /* Note: Only possible reason for error exit is ENOMEM
     */
    errno = ENOMEM;
    return NULL;
}

static bool valid_encode_flags (int flags)
{
    int count = 0;
    int possible_flags = TASKMAP_ENCODE_WRAPPED
                         | TASKMAP_ENCODE_PMI
                         | TASKMAP_ENCODE_RAW
                         | TASKMAP_ENCODE_RAW_DERANGED;
    if ((flags & possible_flags) != flags)
        return false;
    while (flags) {
        flags = flags & (flags - 1); /* clear the least significant bit set */
        count++;
    }
    if (count > 1)
        return false;
    return true;
}

static char *taskmap_encode_map (const struct taskmap *map, int flags)
{
    char *s;
    json_t *taskmap;
    if (!(taskmap = taskmap_encode_json (map, flags)))
        return NULL;
    if (!(s = json_dumps (taskmap, JSON_COMPACT)))
        errno = ENOMEM;
    ERRNO_SAFE_WRAP (json_decref, taskmap);
    return s;
}

static char *list_join (zlistx_t *l, char *sep)
{
    char *result = NULL;
    char *s;
    int seplen = strlen (sep);
    int len = 0;
    int n = 0;

    /* special case: zero length list returns zero length string
    */
    if (zlistx_size (l) == 0)
        return strdup ("");

    s = zlistx_first (l);
    while (s) {
        len += strlen (s) + seplen;
        s = zlistx_next (l);
    }
    if (!(result = malloc (len+1)))
        return NULL;

    s = zlistx_first (l);
    n = 0;
    while (s) {
        n += sprintf (result+n, "%s%s", s, sep);
        s = zlistx_next (l);
    }
    result[len - seplen] = '\0';
    return result;
}

static char *taskmap_encode_raw (const struct taskmap *map, int flags)
{
    char *result = NULL;
    zlistx_t *l;
    int nnodes;
    int saved_errno;

    if (!(l = zlistx_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    zlistx_set_destructor (l, item_destructor);

    nnodes = taskmap_nnodes (map);
    for (int i = 0; i < nnodes; i++) {
        const struct idset *ids = NULL;
        char *s = NULL;
        if (!(ids = taskmap_taskids (map, i))
            || !(s = idset_encode (ids, flags))
            || append_to_zlistx (l, s) < 0) {
            ERRNO_SAFE_WRAP (free, s);
            goto error;
        }
    }
    result = list_join (l, ";");
error:
    saved_errno = errno;
    zlistx_destroy (&l);
    errno = saved_errno;
    return result;
}

static char *taskmap_encode_pmi (const struct taskmap *map)
{
    char *result = NULL;
    char *s;
    struct taskmap_block *block;
    zlistx_t *l;
    int saved_errno;

    if (taskmap_unknown (map))
        return strdup ("");

    if (!(l = zlistx_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    zlistx_set_destructor (l, item_destructor);

    block = zlistx_first (map->blocklist);
    while (block) {
        for (int i = 0; i < block->repeat; i++)
            if (asprintf (&s,
                          "(%d,%d,%d)",
                          block->start,
                          block->nnodes,
                          block->ppn) < 0
                || append_to_zlistx (l, s) < 0)
                goto error;
        block = zlistx_next (map->blocklist);
    }
    if (!(s = list_join (l, ",")))
        goto error;
    if (asprintf (&result, "(vector,%s)", s) < 0)
        result = NULL;
error:
    saved_errno = errno;
    free (s);
    zlistx_destroy (&l);
    errno = saved_errno;
    return result;
}

char *taskmap_encode (const struct taskmap *map, int flags)
{
    if (!map || !valid_encode_flags (flags)) {
        errno = EINVAL;
        return NULL;
    }
    if (flags & TASKMAP_ENCODE_RAW)
        return taskmap_encode_raw (map, IDSET_FLAG_RANGE);
    if (flags & TASKMAP_ENCODE_RAW_DERANGED)
        return taskmap_encode_raw (map, 0);
    if (flags & TASKMAP_ENCODE_PMI)
        return taskmap_encode_pmi (map);
    return taskmap_encode_map (map, flags);
}

int taskmap_check (const struct taskmap *old,
                   const struct taskmap *new,
                   flux_error_t *errp)
{
    int nnodes_old, nnodes_new;
    int ntasks_old, ntasks_new;
    if (!old || !new)
        return errprintf (errp, "Invalid argument");
    nnodes_old = taskmap_nnodes (old);
    nnodes_new = taskmap_nnodes (new);
    if (nnodes_old != nnodes_new)
        return errprintf (errp,
                          "got %d nodes, expected %d",
                          nnodes_new,
                          nnodes_old);
    ntasks_old = taskmap_total_ntasks (old);
    ntasks_new = taskmap_total_ntasks (new);
    if (ntasks_old != ntasks_new)
        return errprintf (errp,
                          "got %d total tasks, expected %d",
                          ntasks_new,
                          ntasks_old);
    return 0;
}

// vi: ts=4 sw=4 expandtab

/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* rutil.c - random standalone helper functions */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libidset/idset.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/dirwalk.h"
#include "src/common/libutil/read_all.h"

#include "rutil.h"

int rutil_idset_sub (struct idset *ids1, const struct idset *ids2)
{
    if (!ids1) {
        errno = EINVAL;
        return -1;
    }
    if (ids2) {
        unsigned int id;
        id = idset_first (ids2);
        while (id != IDSET_INVALID_ID) {
            if (idset_clear (ids1, id) < 0)
                return -1;
            id = idset_next (ids2, id);
        }
    }
    return 0;
}

int rutil_idset_add (struct idset *ids1, const struct idset *ids2)
{
    if (!ids1) {
        errno = EINVAL;
        return -1;
    }
    if (ids2) {
        unsigned int id;
        id = idset_first (ids2);
        while (id != IDSET_INVALID_ID) {
            if (idset_set (ids1, id) < 0)
                return -1;
            id = idset_next (ids2, id);
        }
    }
    return 0;
}

int rutil_idset_diff (const struct idset *ids1,
                      const struct idset *ids2,
                      struct idset **addp,
                      struct idset **subp)
{
    struct idset *add = NULL;
    struct idset *sub = NULL;
    unsigned int id;

    if (!addp || !subp) {
        errno = EINVAL;
        return -1;
    }
    if (ids1) { // find ids in ids1 but not in ids2, and add to 'sub'
        id = idset_first (ids1);
        while (id != IDSET_INVALID_ID) {
            if (!ids2 || !idset_test (ids2, id)) {
                if (!sub && !(sub = idset_create (0, IDSET_FLAG_AUTOGROW)))
                    goto error;
                if (idset_set (sub, id) < 0)
                    goto error;
            }
            id = idset_next (ids1, id);
        }
    }
    if (ids2) { // find ids in ids2 but not in ids1, and add to 'add'
        id = idset_first (ids2);
        while (id != IDSET_INVALID_ID) {
            if (!ids1 || !idset_test (ids1, id)) {
                if (!add && !(add = idset_create (0, IDSET_FLAG_AUTOGROW)))
                    goto error;
                if (idset_set (add, id) < 0)
                    goto error;
            }
            id = idset_next (ids2, id);
        }
    }
    *addp = add;
    *subp = sub;
    return 0;
error:
    idset_destroy (add);
    idset_destroy (sub);
    return -1;
}

int rutil_set_json_idset (json_t *o, const char *key, const struct idset *ids)
{
    json_t *val = NULL;
    char *s = NULL;

    if (o == NULL || key == NULL || strlen (key) == 0) {
        errno = EINVAL;
        return -1;
    }
    if (ids && !(s = idset_encode (ids, IDSET_FLAG_RANGE)))
        return -1;
    if (!(val = json_string (s ? s : "")))
        goto nomem;
    if (json_object_set_new (o, key, val) < 0)
        goto nomem;
    free (s);
    return 0;
nomem:
    errno = ENOMEM;
    ERRNO_SAFE_WRAP (free, s);
    ERRNO_SAFE_WRAP (json_decref, val);
    return -1;
}

struct idset *rutil_idset_from_resobj (const json_t *resobj)
{
    struct idset *ids;
    const char *key;
    json_t *val;

    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;
    if (resobj) {
        json_object_foreach ((json_t *)resobj, key, val) {
            struct idset *valset;
            unsigned long id;

            if (!(valset = idset_decode (key)))
                goto error;
            id = idset_first (valset);
            while (id != IDSET_INVALID_ID) {
                if (idset_set (ids, id) < 0) {
                    idset_destroy (valset);
                    goto error;
                }
                id = idset_next (valset, id);
            }
            idset_destroy (valset);
        }
    }
    return ids;
error:
    idset_destroy (ids);
    return NULL;
}

json_t *rutil_resobj_sub (const json_t *resobj, const struct idset *ids)
{
    const char *key;
    json_t *val;
    json_t *resobj2;

    if (!resobj) {
        errno = EINVAL;
        return NULL;
    }
    if (!(resobj2 = json_object ())) {
        errno = ENOMEM;
        return NULL;
    }
    json_object_foreach ((json_t *)resobj, key, val) {
        struct idset *valset;
        char *key2;

        if (!(valset = idset_decode (key)))
            goto error;
        if (rutil_idset_sub (valset, ids) < 0)
            goto error;
        if (idset_count (valset) > 0) {
            if (!(key2 = idset_encode (valset, IDSET_FLAG_RANGE))) {
                idset_destroy (valset);
                goto error;
            }
            if (json_object_set (resobj2, key2, val) < 0) {
                idset_destroy (valset);
                ERRNO_SAFE_WRAP (free, key2);
                goto error;
            }
            free (key2);
        }
        idset_destroy (valset);
    }
    return resobj2;
error:
    ERRNO_SAFE_WRAP (json_decref, resobj2);
    return NULL;
}

bool rutil_idset_decode_test (const char *idset, unsigned long id)
{
    struct idset *ids;
    bool result;

    if (!(ids = idset_decode (idset)))
        return false;
    result = idset_test (ids, id);
    idset_destroy (ids);
    return result;
}

bool rutil_match_request_sender (const flux_msg_t *msg1,
                                 const flux_msg_t *msg2)
{
    char *sender1 = NULL;
    char *sender2 = NULL;
    bool match = false;

    if (!msg1 || !msg2)
        goto done;
    if (flux_msg_get_route_first (msg1, &sender1) < 0)
        goto done;
    if (flux_msg_get_route_first (msg2, &sender2) < 0)
        goto done;
    if (!sender1 || !sender2)
        goto done;
    if (!strcmp (sender1, sender2))
        match = true;
done:
    free (sender1);
    free (sender2);
    return match;
}

char *rutil_read_file (const char *path, char *errbuf, int errbufsize)
{
    int fd;
    char *buf;

    if ((fd = open (path, O_RDONLY)) < 0 || read_all (fd, (void **)&buf) < 0) {
        snprintf (errbuf, errbufsize, "%s: %s", path, strerror (errno));
        if (fd >= 0)
            ERRNO_SAFE_WRAP (close, fd);
        return NULL;
    }
    close (fd);
    return buf;
}

json_t *rutil_load_file (const char *path, char *errbuf, int errbufsize)
{
    json_t *o;
    json_error_t e;

    if (!(o = json_load_file (path, 0, &e))) {
        snprintf (errbuf, errbufsize, "%s:%d %s", e.source, e.line, e.text);
        errno = EPROTO;
        if (access (path, R_OK) < 0)
            errno = ENOENT;
        return NULL;
    }
    return o;
}

static int set_string (json_t *o, const char *key, const char *val)
{
    json_t *oval;

    if (!(oval = json_string (val)))
        goto nomem;
    if (json_object_set_new (o, key, oval) < 0) {
        json_decref (oval);
        goto nomem;
    }
    return 0;
nomem:
    errno = ENOMEM;
    return -1;
}

static const char *get_error (json_t *o)
{
    int saved_errno;
    const char *s;
    int rc;

    saved_errno = errno;
    rc = json_unpack (o, "{s:s}", "errstr", &s);
    (void)json_object_del (o, "errstr");
    errno = saved_errno;
    if (rc < 0)
        return NULL;
    return s;
}

static void set_error (json_t *o, const char *errstr)
{
    (void)set_string (o, "errstr", errstr);
}

static int load_xml_file (dirwalk_t *d, void *arg)
{
    json_t *o = arg;
    const char *name = dirwalk_name (d);
    char *endptr;
    int rank;
    char *s;
    char key[32];
    char errbuf[256];

    /* Only pay attention to files ending in "<rank<.xml"
     */
    if (dirwalk_isdir (d))
        return 0;
    errno = 0;
    rank = strtol (name, &endptr, 10);
    if (errno > 0 || strcmp (endptr, ".xml") != 0)
        return 0;

    /* Read the file and encode as JSON string, storing under rank key.
     * On error, store human readable error string in object and stop iteration.
     */
    if (!(s = rutil_read_file (dirwalk_path (d), errbuf, sizeof (errbuf)))) {
        dirwalk_stop (d, errno);
        set_error (o, errbuf);
        return 0;
    }
    snprintf (key, sizeof (key), "%d", rank);
    if (set_string (o, key, s) < 0) {
        dirwalk_stop (d, errno);
        free (s);
        return 0;
    }
    free (s);
    return 0;
}

json_t *rutil_load_xml_dir (const char *path, char *errbuf, int errbufsize)
{
    json_t *o;

    if (!(o = json_object ())) {
        errno = ENOMEM;
        return NULL;
    }
    if (dirwalk (path, 0, load_xml_file, o) < 0) {
        const char *errstr = get_error (o);
        snprintf (errbuf,
                  errbufsize,
                  "%s: %s",
                  path,
                  errstr ? errstr : strerror (errno));
        ERRNO_SAFE_WRAP (json_decref, o);
        return NULL;
    }
    if (json_object_size (o) == 0) {
        snprintf (errbuf,
                  errbufsize,
                  "%s: invalid directory: no XML input files found",
                  path);
        json_decref (o);
        errno = EINVAL;
        return NULL;
    }
    return o;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

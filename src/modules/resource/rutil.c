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
#include <unistd.h>
#include <fcntl.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libidset/idset.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/dirwalk.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

#include "rutil.h"

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

char *rutil_read_file (const char *path, flux_error_t *errp)
{
    int fd;
    char *buf;

    if ((fd = open (path, O_RDONLY)) < 0 || read_all (fd, (void **)&buf) < 0) {
        errprintf (errp, "%s: %s", path, strerror (errno));
        if (fd >= 0)
            ERRNO_SAFE_WRAP (close, fd);
        return NULL;
    }
    close (fd);
    return buf;
}

json_t *rutil_load_file (const char *path, flux_error_t *errp)
{
    json_t *o;
    json_error_t e;

    if (!(o = json_load_file (path, 0, &e))) {
        errprintf (errp, "%s:%d %s", e.source, e.line, e.text);
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
    flux_error_t error;

    /* Only pay attention to files ending in "<rank<.xml"
     */
    if (dirwalk_isdir (d))
        return 0;
    errno = 0;
    rank = strtol (name, &endptr, 10);
    if (errno > 0 || !streq (endptr, ".xml"))
        return 0;

    /* Read the file and encode as JSON string, storing under rank key.
     * On error, store human readable error string in object and stop iteration.
     */
    if (!(s = rutil_read_file (dirwalk_path (d), &error))) {
        dirwalk_stop (d, errno);
        set_error (o, error.text);
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

json_t *rutil_load_xml_dir (const char *path, flux_error_t *errp)
{
    json_t *o;

    if (!(o = json_object ())) {
        errno = ENOMEM;
        return NULL;
    }
    if (dirwalk (path, 0, load_xml_file, o) < 0) {
        const char *errstr = get_error (o);
        errprintf (errp,
                   "%s: %s",
                   path,
                   errstr ? errstr : strerror (errno));
        ERRNO_SAFE_WRAP (json_decref, o);
        return NULL;
    }
    if (json_object_size (o) == 0) {
        errprintf (errp,
                   "%s: invalid directory: no XML input files found",
                   path);
        json_decref (o);
        errno = EINVAL;
        return NULL;
    }
    return o;
}

int rutil_idkey_map (json_t *obj, rutil_idkey_map_f map, void *arg)
{
    const char *key;
    json_t *val;
    struct idset *idset;
    unsigned int id;

    json_object_foreach (obj, key, val) {
        if (!(idset = idset_decode (key)))
            return -1;
        id = idset_first (idset);
        while (id != IDSET_INVALID_ID) {
            if (map (id, val, arg) < 0) {
                idset_destroy (idset);
                return -1;
            }
            id = idset_next (idset, id);
        }
        idset_destroy (idset);
    }
    return 0;
}

static int idkey_count_map (unsigned int id, json_t *val, void *arg)
{
    int *count = arg;
    (*count)++;
    return 0;
}

int rutil_idkey_count (json_t *obj)
{
    int count = 0;
    (void)rutil_idkey_map (obj, idkey_count_map, &count);
    return count;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

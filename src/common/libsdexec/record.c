/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* record.c - durable per-unit records kept outside systemd
 *
 * See record.h for the record format.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <jansson.h>

#include "ccan/str/str.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/sigutil.h"
#include "src/common/libeventlog/eventlog.h"

#include "record.h"

static const char *tmp_ext = ".tmp";

/* The ExecStopPost command.  systemd exports the unit's result to it in
 * the environment (systemd.exec(5)); the record path is passed as a
 * positional argument so no shell quoting of the path is involved.  The
 * values are fixed words, numbers or signal names chosen by systemd, not
 * by the unit, so they need no JSON escaping.  The event is appended only
 * if the record still exists: sdexec may have finished with the unit and
 * removed it (e.g. after abandoning a unit stuck in deactivating), and an
 * exit event on its own would be a record with no start.
 */
static const char *exit_script =
    "if test -f \"$1\"; then"
    " printf '{\"timestamp\":%s,\"name\":\"exit\",\"context\":"
    "{\"result\":\"%s\",\"code\":\"%s\",\"status\":\"%s\"}}\\n'"
    " \"$(date +%s.%N)\""
    " \"$SERVICE_RESULT\" \"$EXIT_CODE\" \"$EXIT_STATUS\" >>\"$1\";"
    " fi";

static char *record_path (const char *dir, const char *name, const char *ext)
{
    char *path;

    if (asprintf (&path, "%s/%s%s", dir, name, ext ? ext : "") < 0)
        return NULL;
    return path;
}

static bool name_ok (const char *name)
{
    return name
        && strlen (name) > 0
        && !strchr (name, '/')
        && !streq (name, ".")
        && !streq (name, "..");
}

int sdexec_record_write (const char *dir, const char *name, json_t *context)
{
    char *path = NULL;
    char *tmp = NULL;
    json_t *entry = NULL;
    char *s = NULL;
    int fd = -1;

    if (!dir || !name_ok (name) || !json_is_object (context)) {
        errno = EINVAL;
        return -1;
    }
    if (!(path = record_path (dir, name, NULL))
        || !(tmp = record_path (dir, name, tmp_ext)))
        goto error;
    if (!(entry = eventlog_entry_pack (0., "start", "O", context))
        || !(s = eventlog_entry_encode (entry)))
        goto error;
    if ((fd = open (tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0
        || write_all (fd, s, strlen (s)) < 0
        || close (fd) < 0)
        goto error;
    fd = -1;
    if (rename (tmp, path) < 0)
        goto error;
    free (s);
    json_decref (entry);
    free (tmp);
    free (path);
    return 0;
error:
    if (fd >= 0)
        ERRNO_SAFE_WRAP (close, fd);
    if (tmp)
        ERRNO_SAFE_WRAP (unlink, tmp);
    ERRNO_SAFE_WRAP (free, s);
    ERRNO_SAFE_WRAP (json_decref, entry);
    ERRNO_SAFE_WRAP (free, tmp);
    ERRNO_SAFE_WRAP (free, path);
    return -1;
}

/* Read the record for unit 'name' and decode it one line at a time, so
 * that a damaged line is confined to the event it holds.  Return a JSON
 * array with one element per line: the decoded event, or JSON null if the
 * line is not a valid event.  A final line without a newline (an append
 * cut short) is included as null.  Return NULL with errno set on failure.
 */
static json_t *record_load (const char *dir, const char *name)
{
    char *path;
    int fd;
    void *buf = NULL;
    ssize_t len;
    json_t *a = NULL;
    char *line;
    char *nl;

    if (!(path = record_path (dir, name, NULL)))
        return NULL;
    if ((fd = open (path, O_RDONLY)) < 0) {
        ERRNO_SAFE_WRAP (free, path);
        return NULL;
    }
    len = read_all (fd, &buf);
    ERRNO_SAFE_WRAP (close, fd);
    ERRNO_SAFE_WRAP (free, path);
    if (len < 0)
        return NULL;
    if (!(a = json_array ()))
        goto nomem;
    /* read_all() NUL-terminates the buffer (one byte past len) */
    line = buf;
    while (*line != '\0') {
        json_t *entry = NULL;

        if ((nl = strchr (line, '\n'))) {
            *nl = '\0';
            entry = json_loads (line, 0, NULL);
            if (entry && eventlog_entry_parse (entry, NULL, NULL, NULL) < 0) {
                json_decref (entry);
                entry = NULL;
            }
        }
        if (json_array_append_new (a, entry ? entry : json_null ()) < 0)
            goto nomem;
        if (!nl)
            break;
        line = nl + 1;
    }
    free (buf);
    return a;
nomem:
    free (buf);
    json_decref (a);
    errno = ENOMEM;
    return NULL;
}

json_t *sdexec_record_read (const char *dir, const char *name)
{
    json_t *a;
    const char *ename;
    json_t *context;

    if (!dir || !name_ok (name)) {
        errno = EINVAL;
        return NULL;
    }
    if (!(a = record_load (dir, name)))
        return NULL;
    if (eventlog_entry_parse (json_array_get (a, 0), NULL, &ename, &context) < 0
        || !streq (ename, "start")
        || !context) {
        json_decref (a);
        errno = EPROTO;
        return NULL;
    }
    json_incref (context);
    json_decref (a);
    return context;
}

/* Translate systemd's EXIT_CODE and EXIT_STATUS (systemd.exec(5)) to the
 * ExecMainCode (a CLD_* value) and ExecMainStatus properties the unit
 * object understands.  For a signal death, EXIT_STATUS is the signal name
 * without its SIG prefix.  A core dump is reported as CLD_KILLED since
 * that is the only signal code the unit object maps to a wait status.
 */
static int parse_exit (const char *code,
                       const char *status,
                       int *codep,
                       int *statusp)
{
    if (streq (code, "exited")) {
        char *endptr;
        long l;

        errno = 0;
        l = strtol (status, &endptr, 10);
        if (errno != 0 || *endptr != '\0' || endptr == status || l < 0 || l > 255)
            return -1;
        *codep = CLD_EXITED;
        *statusp = (int)l;
    }
    else if (streq (code, "killed") || streq (code, "dumped")) {
        int signum;

        if ((signum = sigutil_signum (status)) < 0)
            return -1;
        *codep = CLD_KILLED;
        *statusp = signum;
    }
    else
        return -1;
    return 0;
}

/* The exit event is the first event named "exit".  If there is none, any
 * line that could not be decoded may be a damaged exit event, so the
 * outcome is EPROTO (unknown) rather than ENOENT (not yet exited).
 */
json_t *sdexec_record_read_exit (const char *dir, const char *name)
{
    json_t *a;
    size_t index;
    json_t *entry;
    json_t *exit_context = NULL;
    bool damaged = false;
    const char *code;
    const char *status;
    int cld_code;
    int cld_status;
    json_t *o = NULL;

    if (!dir || !name_ok (name)) {
        errno = EINVAL;
        return NULL;
    }
    if (!(a = record_load (dir, name)))
        return NULL;
    json_array_foreach (a, index, entry) {
        const char *ename;
        json_t *context;

        if (json_is_null (entry)) {
            damaged = true;
            continue;
        }
        if (eventlog_entry_parse (entry, NULL, &ename, &context) == 0
            && streq (ename, "exit")) {
            exit_context = context;
            break;
        }
    }
    if (!exit_context) {
        errno = damaged ? EPROTO : ENOENT;
        goto done;
    }
    if (json_unpack (exit_context,
                     "{s:s s:s}",
                     "code", &code,
                     "status", &status) < 0
        || parse_exit (code, status, &cld_code, &cld_status) < 0) {
        errno = EPROTO;
        goto done;
    }
    if (!(o = json_pack ("{s:[si] s:[si]}",
                         "ExecMainCode", "i", cld_code,
                         "ExecMainStatus", "i", cld_status)))
        errno = ENOMEM;
done:
    ERRNO_SAFE_WRAP (json_decref, a);
    return o;
}

int sdexec_record_remove (const char *dir, const char *name)
{
    char *path;
    int rc = 0;

    if (!dir || !name_ok (name)) {
        errno = EINVAL;
        return -1;
    }
    if (!(path = record_path (dir, name, NULL)))
        return -1;
    if (unlink (path) < 0 && errno != ENOENT)
        rc = -1;
    ERRNO_SAFE_WRAP (free, path);
    return rc;
}

/* Call 'fn' for each entry in 'dir' whose name ends in 'suffix' followed
 * by 'ext' (or nothing if 'ext' is NULL).  Stop and return -1 if 'fn'
 * returns -1.
 */
static int record_foreach (const char *dir,
                           const char *suffix,
                           const char *ext,
                           int (*fn) (const char *dir, const char *entry, void *arg),
                           void *arg)
{
    DIR *d;
    struct dirent *ent;
    size_t extlen = ext ? strlen (ext) : 0;
    int rc = 0;

    if (!(d = opendir (dir)))
        return -1;
    while ((ent = readdir (d))) {
        size_t len = strlen (ent->d_name);
        char base[sizeof (ent->d_name)];

        if (streq (ent->d_name, ".") || streq (ent->d_name, ".."))
            continue;
        if (ext) {
            if (len <= extlen || !strends (ent->d_name, ext))
                continue;
        }
        else if (strends (ent->d_name, tmp_ext))
            continue;
        memcpy (base, ent->d_name, len - extlen);
        base[len - extlen] = '\0';
        if (!strends (base, suffix))
            continue;
        if (fn (dir, ent->d_name, arg) < 0) {
            rc = -1;
            break;
        }
    }
    ERRNO_SAFE_WRAP (closedir, d);
    return rc;
}

static int list_append (const char *dir, const char *entry, void *arg)
{
    json_t *a = arg;
    json_t *s;

    if (!(s = json_string (entry)) || json_array_append_new (a, s) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

json_t *sdexec_record_list (const char *dir, const char *suffix)
{
    json_t *a;

    if (!dir || !suffix) {
        errno = EINVAL;
        return NULL;
    }
    if (!(a = json_array ())) {
        errno = ENOMEM;
        return NULL;
    }
    if (record_foreach (dir, suffix, NULL, list_append, a) < 0) {
        ERRNO_SAFE_WRAP (json_decref, a);
        return NULL;
    }
    return a;
}

static int unlink_tmp (const char *dir, const char *entry, void *arg)
{
    int *count = arg;
    char *path;

    if (!(path = record_path (dir, entry, NULL)))
        return -1;
    if (unlink (path) == 0)
        (*count)++;
    free (path);
    return 0;
}

int sdexec_record_remove_tmp (const char *dir, const char *suffix)
{
    int count = 0;

    if (!dir || !suffix) {
        errno = EINVAL;
        return -1;
    }
    if (record_foreach (dir, suffix, tmp_ext, unlink_tmp, &count) < 0)
        return -1;
    return count;
}

json_t *sdexec_record_execstoppost (const char *dir, const char *name)
{
    char *path;
    json_t *a;

    if (!dir || !name_ok (name)) {
        errno = EINVAL;
        return NULL;
    }
    if (!(path = record_path (dir, name, NULL)))
        return NULL;
    /* sh -c 'script' sh <path>: "sh" is $0 and the path is $1 */
    if (!(a = json_pack ("[sssss]", "/bin/sh", "-c", exit_script, "sh", path)))
        errno = ENOMEM;
    ERRNO_SAFE_WRAP (free, path);
    return a;
}

// vi:ts=4 sw=4 expandtab

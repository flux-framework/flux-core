/* curve.c - helper functions for cmbd's use of CURVE security */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>
#include <zmq.h>
#include <czmq.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#if ZMQ_VERSION_MAJOR >= 4
#define HAVE_CURVE_SECURITY 1
#endif

#if HAVE_CURVE_SECURITY
#include "curve.h"

char *flux_curve_getpath (void)
{
    struct passwd *pw = getpwuid (geteuid ());
    char *path = NULL;

	if (!pw || !pw->pw_dir || strlen (pw->pw_dir) == 0) {
        msg ("could not determine home directory for uid %d", geteuid ());
        goto done;
    }
    if (asprintf (&path, "%s/.curve", pw->pw_dir) < 0)
        oom ();
done:
    return path;
}

int flux_curve_checkpath (const char *path, bool create)
{
    struct stat sb;
    int rc = -1;

    if (create && mkdir (path, 0700) < 0) {
        if (errno != EEXIST)
            err ("%s", path);
    }
    if (lstat (path, &sb) < 0) {
        err ("%s", path);
        goto done;
    }
    if (!S_ISDIR (sb.st_mode)) {
        msg ("%s: not a directory", path);
        goto done;
    }
    if ((sb.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO)) != 0700) {
        msg ("%s: permissions not set to 0700", path);
        goto done;
    }
    if ((sb.st_uid != geteuid ())) {
        msg ("%s: invalid owner", path);
        goto done;
    }
    rc = 0;
done:
    return rc;
}

static char * ctime_iso8601_now (char *buf, size_t sz)
{
    struct tm tm;
    time_t now = time (NULL);

    memset (buf, 0, sz);

    if (!localtime_r (&now, &tm))
        return (NULL);
    strftime (buf, sz, "%FT%T", &tm);

    return (buf);
}

static int gencred (const char *dir, const char *session, const char *role,
                    bool force)
{
    char *path = NULL, *priv = NULL;;
    zcert_t *cert = NULL;
    char buf[64];
    struct stat sb;
    int rc = -1;

    if (!session)
        session = "flux";
    if (asprintf (&path, "%s/%s.%s", dir, session, role) < 0)
        oom ();
    if (asprintf (&priv, "%s/%s.%s_private", dir, session, role) < 0)
        oom ();
    if (force) {
        (void)unlink (path);
        (void)unlink (priv);
    }
    if (stat (path, &sb) == 0) {
        msg ("%s exists", path);
        goto done;
    }
    if (stat (priv, &sb) == 0) {
        msg ("%s exists", priv);
        goto done;
    }
    if (!(cert = zcert_new ()))
        oom ();
    zcert_set_meta (cert, "time", "%s", ctime_iso8601_now (buf, sizeof (buf)));
    zcert_set_meta (cert, "role", (char *)role);
    zcert_set_meta (cert, "session", "%s", (char *)session);
    msg ("Saving %s", path);
    msg ("Saving %s", priv);
    if (zcert_save (cert, path) < 0) {
        err ("zcert_save %s", path);
        goto done;
    }
    rc = 0;
done:
    if (cert)
        zcert_destroy (&cert);
    if (path)
        free (path);
    if (priv)
        free (priv);
    return rc;
}

int flux_curve_gencli (const char *dir, const char *session, bool force)
{
    return gencred (dir, session, "client", force);
}
int flux_curve_gensrv (const char *dir, const char *session, bool force)
{
    return gencred (dir, session, "server", force);
}

static zcert_t *getcred (const char *dir, const char *session,
                         const char *role)
{
    char *path = NULL;;
    zcert_t *cert = NULL;

    if (!session)
        session = "flux";
    if (asprintf (&path, "%s/%s.%s", dir, session, role) < 0)
        oom ();
    if (!(cert = zcert_load (path)))
        err ("%s", path);
    free (path);
    return cert;
}

zcert_t *flux_curve_getcli (const char *dir, const char *session)
{
    return getcred (dir, session, "client");
}
zcert_t *flux_curve_getsrv (const char *dir, const char *session)
{
    return getcred (dir, session, "server");
}
#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

/* flux-keygen.c - flux key management subcommand */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>
#include <czmq.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "hfn:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"force",      no_argument,        0, 'f'},
    {"name",       required_argument,  0, 'n'},
    { 0, 0, 0, 0 },
};

static char * ctime_iso8601_now (char *buf, size_t sz);

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-keygen [--force] [--name NAME]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    char *name = "flux";
    struct stat sb;
    int i;
    char *path, *curve_path;
    char *cert_tmpl[] = { "%s/%s.client", "%s/%s.client_secret",
                          "%s/%s.server", "%s/%s.server_secret" };
    const int ncerts = sizeof (cert_tmpl) / sizeof (cert_tmpl[0]);
    bool fopt = false;
    int ch;
    struct passwd *pw;

    log_init ("flux-keygen");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'f': /* --force */
                fopt = true;
                break;
            case 'n': /* --name */
                name = optarg;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind < argc)
        usage ();

    if (!(pw = getpwuid (geteuid ()))
                    || (pw->pw_dir == NULL || strlen (pw->pw_dir) == 0))
        msg_exit ("could not determine home directory");
    if (asprintf (&curve_path, "%s/.curve", pw->pw_dir) < 0)
        oom ();

    /* Ensure that key directory exists and has appropriate permissions.
     */
    if (mkdir (curve_path, 0700) < 0) {
        if (errno != EEXIST)
            err_exit ("%s", curve_path);
    }
    if (lstat (curve_path, &sb) < 0) /* don't follow symlinks */
        err_exit ("%s", curve_path);
    if (!S_ISDIR (sb.st_mode))
        msg_exit ("%s: not a directory", curve_path);
    if ((sb.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO)) != 0700)
        msg_exit ("%s: permissions not set to 0700", curve_path);
    if ((sb.st_uid != geteuid ()))
        msg_exit ("%s: invalid owner", curve_path);
    /* Ensure that none of the cert files for this session exist
     * before proceeding.
     */
    for (i = 0; i < ncerts; i++) {
        if (asprintf (&path, cert_tmpl[i], curve_path, name) < 0)
            oom ();
        if (lstat (path, &sb) < 0) {
            if (errno != ENOENT)
                err_exit (path);
        } else {
            if (!fopt)
                msg_exit ("%s exists, use --force to regen", path);
            if (unlink (path) < 0)
                err_exit ("unlink %s", path);
        }
        free (path);
    }
    /* Create certs.
     */
    for (i = 0; i < ncerts; i += 2) { /* skip _secret */
        zcert_t *cert;
        char buf[64];

        if (!(cert = zcert_new ()))
            oom (); /* FIXME: other errors possible? check source */
        zcert_set_meta (cert, "user", "%s", pw->pw_name);
        zcert_set_meta (cert, "time", "%s",
                        ctime_iso8601_now (buf, sizeof (buf)));
        zcert_set_meta (cert, "role", "flux-%s", i == 0 ? "client" : "server");
        zcert_set_meta (cert, "session", "%s", name);
        if (asprintf (&path, cert_tmpl[i], curve_path, name) < 0)
            oom ();
        msg ("Saving cert to %s[_secret]", path);
        if (zcert_save (cert, path) < 0)
            err_exit ("zcert_save %s", path);
        zcert_destroy (&cert);
        free (path);
    }
    /* Verify that certs can be loaded.
     */
    for (i = 0; i < ncerts; i += 2) { /* skip _secret */
        zcert_t *cert;

        if (!(cert = zcert_new ()))
            oom (); /* FIXME: other errors possible? check source */
        if (asprintf (&path, cert_tmpl[i], curve_path, name) < 0)
            oom ();
        if (!(cert = zcert_load (path)))
            err_exit ("%s[_secret]", path);
        zcert_destroy (&cert);
        free (path);
    }

    free (curve_path);

    log_fini ();
    
    return 0;
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

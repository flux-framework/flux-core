/* tkvs.c - exercise basic kvs functions */

/* Usage: tkvs [OPTIONS] operation [key [val]] */

#include <sys/types.h>
#include <sys/time.h>
#include <json/json.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <zmq.h>
#include <czmq.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"
#include "zmsg.h"

#define OPTIONS "ZFDc:"
static const struct option longopts[] = {
   {"trace-apisock", no_argument,        0, 'Z'},
   {"get-fileval",   no_argument,        0, 'F'},
   {"get-dirval",    no_argument,        0, 'D'},
   {"chdir",         required_argument,  0, 'c'},
   {0, 0, 0, 0},
};


static void usage (void)
{
    fprintf (stderr, "%s", "\
Usage: tkvs OPTIONS op [key] [val]\n\
\n\
Where OPTIONS can be one of\n\
    -Z,--trace-apisock\n\
    -d,--deep\n\
The possible operations are:\n\
    get key\n\
    put key val\n\
    get_string key\n\
    put_string key val\n\
    get_int key\n\
    put_int key val\n\
    get_int64 key\n\
    put_int64 key val\n\
    get_double key\n\
    put_double key val\n\
    get_boolean key\n\
    put_booelan key val (use \"true\" or \"false\")\n\
    symlink key val\n\
    mkdir key\n\
    unlink key\n\
    get_dir key\n\
    get_dir_r key\n\
    get_all key\n\
    get_all_r key\n\
    get_symlink key\n\
    get_version\n\
    wait_version int\n\
    commit\n\
");
    exit (1);
}

void tkvs_get_version (cmb_t c)
{
    int version;

    if (kvs_get_version (c, &version) < 0)
        err_exit ("kvs_get_version");
    printf ("%d\n", version);
}

void tkvs_wait_version (cmb_t c, int version)
{
    if (kvs_wait_version (c, version) < 0)
        err_exit ("kvs_wait_version");
}

void tkvs_mkdir (cmb_t c, char *key)
{
    if (kvs_mkdir (c, key) < 0)
        err_exit ("kvs_mkdir %s", key);
}

void tkvs_symlink(cmb_t c, char *key, char *val)
{
    if (kvs_symlink (c, key, val) < 0)
        err_exit ("kvs_symlink %s", key);
}

void tkvs_unlink (cmb_t c, char *key)
{
    if (kvs_unlink (c, key) < 0)
        err_exit ("kvs_unlink %s", key);
}

void tkvs_get (cmb_t c, char *key)
{
    json_object *o;

    if (kvs_get (c, key, &o) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else
            err_exit ("kvs_get %s", key);
    } else {
        printf ("%s\n", json_object_to_json_string (o));
        json_object_put (o);
    }
}

void tkvs_put (cmb_t c, char *key, char *val)
{
    json_object *o;

    if (!(o = json_tokener_parse (val)))
        msg_exit ("error parsing json value");
    if (kvs_put (c, key, o) < 0)
        err_exit ("kvs_put %s=%s", key, val);
    json_object_put (o);
}

void tkvs_dump_dir (kvsdir_t dir, bool ropt)
{
    kvsitr_t itr = kvsitr_create (dir);
    const char *name;
    char *key;

    while ((name = kvsitr_next (itr))) {
        key = kvsdir_key_at (dir, name);
        if (kvsdir_isdir (dir, name)) {
            if (ropt) {
                kvsdir_t ndir;
                if (kvsdir_get_dir (dir, &ndir, "%s", name) < 0)
                    err_exit ("kvsdir_get_dir %s", key);
                tkvs_dump_dir (ndir, ropt); 
                kvsdir_destroy (ndir);
            } else
                printf ("%s{%s}\n", key, "dir");
        } else  {
            printf ("%s{%s}\n", key,
                    kvsdir_issymlink (dir, name) ? "symlink"
                  : kvsdir_isstring (dir, name) ? "string"
                  : kvsdir_isint (dir, name) ? "int"
                  : kvsdir_isint64 (dir, name) ? "int64"
                  : kvsdir_isdouble (dir, name) ? "double"
                  : kvsdir_isboolean (dir, name) ? "boolean" : "JSON");
        }
    }
    kvsitr_destroy (itr);
}

void tkvs_dump_all (kvsdir_t dir, bool ropt)
{
    kvsitr_t itr = kvsitr_create (dir);
    const char *name;
    char *key;

    while ((name = kvsitr_next (itr))) {
        key = kvsdir_key_at (dir, name);
        if (kvsdir_issymlink (dir, name)) {
            char *s;
            if (kvsdir_get_symlink (dir, name, &s) < 0)
                err_exit ("kvsdir_get_symlink %s", key);
            printf ("%s -> %s\n", key, s);
            free (s);
        } else if (kvsdir_isdir (dir, name)) {
            if (ropt) {
                kvsdir_t ndir;
                if (kvsdir_get_dir (dir, &ndir, "%s", name) < 0)
                    err_exit ("kvsdir_get_dir %s", key);
                tkvs_dump_all (ndir, ropt); 
                kvsdir_destroy (ndir);
            } else
                printf ("%s{%s}\n", key, "dir");
        } else if (kvsdir_isstring (dir, name)) {
            char *s;
            if (kvsdir_get_string (dir, name, &s) < 0)
                err_exit ("kvsdir_get_string %s", key);
            printf ("%s = %s\n", key, s);
            free (s);
        } else if (kvsdir_isint (dir, name)) {
            int i;
            if (kvsdir_get_int (dir, name, &i) < 0)
                err_exit ("kvsdir_get_int64 %s", key);
            printf ("%s = %d\n", key, i);
        } else if (kvsdir_isint64 (dir, name)) {
            int64_t i;
            if (kvsdir_get_int64 (dir, name, &i) < 0)
                err_exit ("kvsdir_get_int64 %s", key);
            printf ("%s = %lld\n", key, (long long int)i);
        } else if (kvsdir_isdouble (dir, name)) {
            double n;
            if (kvsdir_get_double (dir, name, &n) < 0)
                err_exit ("kvsdir_get_double %s", key);
            printf ("%s = %lf\n", key, n);
        } else if (kvsdir_isboolean (dir, name)) {
            bool b;
            if (kvsdir_get_boolean (dir, name, &b) < 0)
                err_exit ("kvsdir_get_boolean %s", key);
            printf ("%s = %s\n", key, b ? "true" : "false");
        } else {
            json_object *o;
            if (kvsdir_get (dir, name, &o) < 0)
                err_exit ("kvsdir_get %s", key);
            printf ("%s = %s\n", key, json_object_to_json_string (o));
            json_object_put (o);
        }
        free (key);
    }
    kvsitr_destroy (itr);
}

void tkvs_get_dir (cmb_t c, char *key, bool ropt, bool all, int flags)
{
    kvsdir_t dir;

    if (kvs_get_dir (c, flags, &dir, "%s", key) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else
            err_exit ("kvs_get %s", key);
    } else {
        if (all)
            tkvs_dump_all (dir, ropt);
        else 
            tkvs_dump_dir (dir, ropt);
        kvsdir_destroy (dir);
    }
}

void tkvs_get_symlink (cmb_t c, char *key)
{
    char *val;

    if (kvs_get_symlink (c, key, &val) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else
            err_exit ("kvs_get_symlink %s", key);
    } else {
        printf ("%s\n", val);
        free (val);
    }
}


void tkvs_get_string (cmb_t c, char *key)
{
    char *val;

    if (kvs_get_string (c, key, &val) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else
            err_exit ("kvs_get_string %s", key);
    } else {
        printf ("%s\n", val);
        free (val);
    }
}

void tkvs_put_string (cmb_t c, char *key, char *val)
{
    if (kvs_put_string (c, key, val) < 0)
        err_exit ("kvs_put_string %s=%s", key, val);
}

void tkvs_get_int (cmb_t c, char *key)
{
    int val;

    if (kvs_get_int (c, key, &val) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else
            err_exit ("kvs_get_int %s", key);
    } else
        printf ("%d\n", val);
}

void tkvs_put_int (cmb_t c, char *key, int val)
{
    if (kvs_put_int (c, key, val) < 0)
        err_exit ("kvs_put_int %s=%d", key, val);
}

void tkvs_get_int64 (cmb_t c, char *key)
{
    int64_t val;

    if (kvs_get_int64 (c, key, &val) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else 
            err_exit ("kvs_get_int64 %s", key);
    } else
        printf ("%lld\n", (long long int) val);
}

void tkvs_put_int64 (cmb_t c, char *key, int64_t val)
{
    if (kvs_put_int64 (c, key, val) < 0)
        err_exit ("kvs_put_int64 %s=%lld", key, (long long int)val);
}

void tkvs_get_double  (cmb_t c, char *key)
{
    double val;

    if (kvs_get_double (c, key, &val) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else
            err_exit ("kvs_get_double  %s", key);
    } else
        printf ("%f\n", val);
}

void tkvs_put_double (cmb_t c, char *key, double val)
{
    if (kvs_put_double (c, key, val) < 0)
        err_exit ("kvs_put_double %s=%lf", key, val);
}

void tkvs_get_boolean (cmb_t c, char *key)
{
    bool val;

    if (kvs_get_boolean (c, key, &val) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else
            err_exit ("cmb_kvs_get_boolean %s", key);
    } else
        printf ("%s\n", val ? "true" : "false");
}

void tkvs_put_boolean (cmb_t c, char *key, bool val)
{
    if (kvs_put_boolean (c, key, val) < 0)
        err_exit ("kvs_put_boolean %s=%s", key, val ? "true" : "false");
}
void tkvs_commit (cmb_t c)
{
    if (kvs_commit (c) < 0)
        err_exit ("kvs_commit");
}

int main (int argc, char *argv[])
{
    int ch;
    char *op = NULL;
    char *key = NULL;
    char *val = NULL;
    cmb_t c;
    char path[PATH_MAX + 1];
    int cmb_flags = 0;
    int kvs_flags = 0;
    char *chdir = NULL;

    log_init (basename (argv[0]));
    snprintf (path, sizeof (path), CMB_API_PATH_TMPL, getuid ());
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'Z': /* --trace-apisock */
                cmb_flags |= CMB_FLAGS_TRACE;
                break;
            case 'F': /* --get-fileval */
                kvs_flags |= KVS_GET_FILEVAL;
                break;
            case 'D': /* --get-dirval */
                kvs_flags |= KVS_GET_DIRVAL;
                break;
            case 'c': /* --chdir DIR */
                chdir = optarg;
                break;
            default:
                usage ();
        }
    }
    if (optind < argc)
        op = argv[optind++];
    if (optind < argc)
        key = argv[optind++];
    if (optind < argc)
        val = argv[optind++];
    if (!op)
        usage ();
    if (!(c = cmb_init_full (path, cmb_flags)))
        err_exit ("cmb_init");

    if (chdir)
        kvs_chdir (c, chdir);

    if (!strcmp (op, "get_string") && key)
        tkvs_get_string (c, key);
    else if (!strcmp (op, "put_string") && key && val)
        tkvs_put_string (c, key, val);

    else if (!strcmp (op, "get_int") && key)
        tkvs_get_int (c, key);
    else if (!strcmp (op, "put_int") && key && val)
        tkvs_put_int (c, key, strtoul (val, NULL, 10));

    else if (!strcmp (op, "get_int64") && key)
        tkvs_get_int64 (c, key);
    else if (!strcmp (op, "put_int64") && key && val)
        tkvs_put_int64 (c, key, strtoull (val, NULL, 10));

    else if (!strcmp (op, "get_double") && key)
        tkvs_get_double (c, key);
    else if (!strcmp (op, "put_double") && key && val)
        tkvs_put_double (c, key, strtod (val, NULL));

    else if (!strcmp (op, "get_boolean") && key)
        tkvs_get_boolean (c, key);
    else if (!strcmp (op, "put_boolean") && key && val)
        tkvs_put_boolean (c, key, !strcmp (val, "false") ? false : true);

    else if (!strcmp (op, "get_dir") && key)
        tkvs_get_dir (c, key, false, false, kvs_flags);
    else if (!strcmp (op, "get_dir_r") && key)
        tkvs_get_dir (c, key, true, false, kvs_flags);

    else if (!strcmp (op, "get_all") && key)
        tkvs_get_dir (c, key, false, true, kvs_flags);
    else if (!strcmp (op, "get_all_r") && key)
        tkvs_get_dir (c, key, true, true, kvs_flags);

    else if (!strcmp (op, "get_symlink") && key)
        tkvs_get_symlink (c, key);

    else if (!strcmp (op, "get") && key)
        tkvs_get (c, key);
    else if (!strcmp (op, "put") && key && val)
        tkvs_put (c, key, val);

    else if (!strcmp (op, "unlink") && key)
        tkvs_unlink (c, key);
    else if (!strcmp (op, "mkdir") && key)
        tkvs_mkdir (c, key);
    else if (!strcmp (op, "symlink") && key && val)
        tkvs_symlink (c, key, val);
    else if (!strcmp (op, "commit"))
        tkvs_commit (c);     

    else if (!strcmp (op, "get_version"))
        tkvs_get_version (c);
    else if (!strcmp (op, "wait_version") && key)
        tkvs_wait_version (c, strtoul (key, NULL, 10));

    else
        usage ();
    cmb_fini (c);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

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

#define OPTIONS "Z"
static const struct option longopts[] = {
   {"trace-apisock", no_argument,        0, 'Z'},
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

void tkvs_get_version (flux_t h)
{
    int version;

    if (kvs_get_version (h, &version) < 0)
        err_exit ("kvs_get_version");
    printf ("%d\n", version);
}

void tkvs_wait_version (flux_t h, int version)
{
    if (kvs_wait_version (h, version) < 0)
        err_exit ("kvs_wait_version");
}

void tkvs_mkdir (flux_t h, char *key)
{
    if (kvs_mkdir (h, key) < 0)
        err_exit ("kvs_mkdir %s", key);
}

void tkvs_symlink(flux_t h, char *key, char *val)
{
    if (kvs_symlink (h, key, val) < 0)
        err_exit ("kvs_symlink %s", key);
}

void tkvs_unlink (flux_t h, char *key)
{
    if (kvs_unlink (h, key) < 0)
        err_exit ("kvs_unlink %s", key);
}

void tkvs_get (flux_t h, char *key)
{
    json_object *o;

    if (kvs_get (h, key, &o) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else
            err_exit ("kvs_get %s", key);
    } else {
        printf ("%s\n", json_object_to_json_string (o));
        json_object_put (o);
    }
}

void tkvs_put (flux_t h, char *key, char *val)
{
    json_object *o;

    if (!(o = json_tokener_parse (val)))
        msg_exit ("error parsing json value");
    if (kvs_put (h, key, o) < 0)
        err_exit ("kvs_put %s=%s", key, val);
    json_object_put (o);
}

void tkvs_dump_dir (flux_t h, const char *path, bool ropt)
{
    kvsitr_t itr;
    kvsdir_t dir;
    const char *name;
    char *key;

    if (kvs_get_dir (h, &dir, "%s", path) < 0)
        err_exit ("kvs_get_dir %s", path);

    itr = kvsitr_create (dir);
    while ((name = kvsitr_next (itr))) {
        key = kvsdir_key_at (dir, name);
        if (kvsdir_issymlink (dir, name)) {
            printf ("%s{symlink}\n", key);
        } else if (kvsdir_isdir (dir, name)) {
            if (ropt)
                tkvs_dump_dir (h, key, ropt); 
            else
                printf ("%s{dir}\n", key);
        } else {
            printf ("%s{value}\n", key);
        }
        free (key);
    }
    kvsitr_destroy (itr);
    kvsdir_destroy (dir);
}

void tkvs_dump_all (flux_t h, const char *path, bool ropt)
{
    kvsitr_t itr;
    kvsdir_t dir;
    const char *name;
    char *key;
    char *s;
    int i;
    int64_t I;
    double n;
    bool b;
    json_object *o;

    if (kvs_get_dir (h, &dir, "%s", path) < 0)
        err_exit ("kvs_get_dir %s", path);

    itr = kvsitr_create (dir);
    while ((name = kvsitr_next (itr))) {
        key = kvsdir_key_at (dir, name);
        if (kvsdir_issymlink (dir, name)) {
            if (kvs_get_symlink (h, key, &s) < 0)
                err_exit ("kvs_get_symlink %s", key);
            printf ("%s -> %s\n", key, s);
            free (s);
        } else if (kvsdir_isdir (dir, name)) {
            if (ropt)
                tkvs_dump_all (h, key, ropt);
            else
                printf ("%s{%s}\n", key, "dir");
        } else if (kvs_get_string (h, key, &s) == 0) {
            printf ("%s = %s\n", key, s);
            free (s);
        } else if (kvs_get_int (h, key, &i) == 0) {
            printf ("%s = %d\n", key, i);
        } else if (kvs_get_int64 (h, key, &I) == 0) {
            printf ("%s = %lld\n", key, (long long int)i);
        } else if (kvs_get_double (h, key, &n) == 0) {
            printf ("%s = %lf\n", key, n);
        } else if (kvs_get_boolean (h, key, &b) == 0) {
            printf ("%s = %s\n", key, b ? "true" : "false");
        } else {
            if (kvs_get (h, key, &o) < 0)
                err_exit ("kvs_get %s", key);
            printf ("%s = %s\n", key, json_object_to_json_string (o));
            json_object_put (o);
        }
        free (key);
    }
    kvsitr_destroy (itr);
    kvsdir_destroy (dir);
}


void tkvs_get_symlink (flux_t h, char *key)
{
    char *val;

    if (kvs_get_symlink (h, key, &val) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else
            err_exit ("kvs_get_symlink %s", key);
    } else {
        printf ("%s\n", val);
        free (val);
    }
}


void tkvs_get_string (flux_t h, char *key)
{
    char *val;

    if (kvs_get_string (h, key, &val) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else
            err_exit ("kvs_get_string %s", key);
    } else {
        printf ("%s\n", val);
        free (val);
    }
}

void tkvs_put_string (flux_t h, char *key, char *val)
{
    if (kvs_put_string (h, key, val) < 0)
        err_exit ("kvs_put_string %s=%s", key, val);
}

void tkvs_get_int (flux_t h, char *key)
{
    int val;

    if (kvs_get_int (h, key, &val) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else
            err_exit ("kvs_get_int %s", key);
    } else
        printf ("%d\n", val);
}

void tkvs_put_int (flux_t h, char *key, int val)
{
    if (kvs_put_int (h, key, val) < 0)
        err_exit ("kvs_put_int %s=%d", key, val);
}

void tkvs_get_int64 (flux_t h, char *key)
{
    int64_t val;

    if (kvs_get_int64 (h, key, &val) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else 
            err_exit ("kvs_get_int64 %s", key);
    } else
        printf ("%lld\n", (long long int) val);
}

void tkvs_put_int64 (flux_t h, char *key, int64_t val)
{
    if (kvs_put_int64 (h, key, val) < 0)
        err_exit ("kvs_put_int64 %s=%lld", key, (long long int)val);
}

void tkvs_get_double  (flux_t h, char *key)
{
    double val;

    if (kvs_get_double (h, key, &val) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else
            err_exit ("kvs_get_double  %s", key);
    } else
        printf ("%f\n", val);
}

void tkvs_put_double (flux_t h, char *key, double val)
{
    if (kvs_put_double (h, key, val) < 0)
        err_exit ("kvs_put_double %s=%lf", key, val);
}

void tkvs_get_boolean (flux_t h, char *key)
{
    bool val;

    if (kvs_get_boolean (h, key, &val) < 0) {
        if (errno == ENOENT)
            printf ("null\n");
        else
            err_exit ("cmb_kvs_get_boolean %s", key);
    } else
        printf ("%s\n", val ? "true" : "false");
}

void tkvs_put_boolean (flux_t h, char *key, bool val)
{
    if (kvs_put_boolean (h, key, val) < 0)
        err_exit ("kvs_put_boolean %s=%s", key, val ? "true" : "false");
}
void tkvs_commit (flux_t h)
{
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");
}

int main (int argc, char *argv[])
{
    int ch;
    char *op = NULL;
    char *key = NULL;
    char *val = NULL;
    flux_t h;
    char path[PATH_MAX + 1];
    int cmb_flags = 0;

    log_init (basename (argv[0]));
    snprintf (path, sizeof (path), CMB_API_PATH_TMPL, getuid ());
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'Z': /* --trace-apisock */
                cmb_flags |= FLUX_FLAGS_TRACE;
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
    if (!(h = cmb_init_full (path, cmb_flags)))
        err_exit ("cmb_init");

    if (!strcmp (op, "get_string") && key)
        tkvs_get_string (h, key);
    else if (!strcmp (op, "put_string") && key && val)
        tkvs_put_string (h, key, val);

    else if (!strcmp (op, "get_int") && key)
        tkvs_get_int (h, key);
    else if (!strcmp (op, "put_int") && key && val)
        tkvs_put_int (h, key, strtoul (val, NULL, 10));

    else if (!strcmp (op, "get_int64") && key)
        tkvs_get_int64 (h, key);
    else if (!strcmp (op, "put_int64") && key && val)
        tkvs_put_int64 (h, key, strtoull (val, NULL, 10));

    else if (!strcmp (op, "get_double") && key)
        tkvs_get_double (h, key);
    else if (!strcmp (op, "put_double") && key && val)
        tkvs_put_double (h, key, strtod (val, NULL));

    else if (!strcmp (op, "get_boolean") && key)
        tkvs_get_boolean (h, key);
    else if (!strcmp (op, "put_boolean") && key && val)
        tkvs_put_boolean (h, key, !strcmp (val, "false") ? false : true);

    else if (!strcmp (op, "get_dir") && key)
        tkvs_dump_dir (h, key, false);
    else if (!strcmp (op, "get_dir_r") && key)
        tkvs_dump_dir (h, key, true);

    else if (!strcmp (op, "get_all") && key)
        tkvs_dump_all (h, key, false);
    else if (!strcmp (op, "get_all_r") && key)
        tkvs_dump_all (h, key, true);

    else if (!strcmp (op, "get_symlink") && key)
        tkvs_get_symlink (h, key);

    else if (!strcmp (op, "get") && key)
        tkvs_get (h, key);
    else if (!strcmp (op, "put") && key && val)
        tkvs_put (h, key, val);

    else if (!strcmp (op, "unlink") && key)
        tkvs_unlink (h, key);
    else if (!strcmp (op, "mkdir") && key)
        tkvs_mkdir (h, key);
    else if (!strcmp (op, "symlink") && key && val)
        tkvs_symlink (h, key, val);
    else if (!strcmp (op, "commit"))
        tkvs_commit (h);     

    else if (!strcmp (op, "get_version"))
        tkvs_get_version (h);
    else if (!strcmp (op, "wait_version") && key)
        tkvs_wait_version (h, strtoul (key, NULL, 10));

    else
        usage ();
    flux_handle_destroy (&h);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

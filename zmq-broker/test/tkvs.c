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

#define OPTIONS "Zw"
static const struct option longopts[] = {
   {"trace-apisock", no_argument,        0, 'Z'},
   {"watch",         no_argument,        0, 'w'},
   {0, 0, 0, 0},
};


static void usage (void)
{
    fprintf (stderr, "%s", "\
Usage: tkvs OPTIONS op [key] [val]\n\
\n\
Where OPTIONS can be one of\n\
    -Z,--trace-apisock\n\
    -w,--watch\n\
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
    mkdir key\n\
    unlink key\n\
    get_dir key\n\
    get_dir_r key\n\
    get_all key\n\
    get_all_r key\n\
    commit\n\
");
    exit (1);
}

void kvs_mkdir (cmb_t c, char *key)
{
    if (cmb_kvs_mkdir (c, key) < 0)
        err_exit ("cmb_kvs_mkdir %s", key);
}

void kvs_unlink (cmb_t c, char *key)
{
    if (cmb_kvs_unlink (c, key) < 0)
        err_exit ("cmb_kvs_unlink %s", key);
}

void kvs_get (cmb_t c, char *key, bool watch)
{
    int flags = 0;
    json_object *o;

    if (watch)
        flags = KVS_GET_WATCH;
    do {
        if (cmb_kvs_get (c, key, &o, flags) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else
                err_exit ("cmb_kvs_get %s", key);
        } else {
            printf ("%s\n", json_object_to_json_string (o));
            json_object_put (o);
        }
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void kvs_put (cmb_t c, char *key, char *val)
{
    json_object *o;

    if (!(o = json_tokener_parse (val)))
        msg_exit ("error parsing json value");
    if (cmb_kvs_put (c, key, o) < 0)
        err_exit ("cmb_kvs_put %s=%s", key, val);
    json_object_put (o);
}

void dump_dir (kvsdir_t dir, bool ropt)
{
    kvsitr_t itr = cmb_kvsitr_create (dir);
    const char *name;
    char *key;

    while ((name = cmb_kvsitr_next (itr))) {
        key = cmb_kvsdir_key_at (dir, name);
        if (cmb_kvsdir_isdir (dir, name)) {
            if (ropt) {
                kvsdir_t ndir;
                if (cmb_kvs_get_dir_at (dir, name, &ndir) < 0)
                    err_exit ("cmb_kvs_get_dir_at %s", key);
                dump_dir (ndir, ropt); 
                cmb_kvsdir_destroy (ndir);
            } else
                printf ("%s{%s}\n", key, "dir");
        } else  {
            printf ("%s{%s}\n", key,
                    cmb_kvsdir_isstring (dir, name) ? "string"
                  : cmb_kvsdir_isint (dir, name) ? "int"
                  : cmb_kvsdir_isint64 (dir, name) ? "int64"
                  : cmb_kvsdir_isdouble (dir, name) ? "double"
                  : cmb_kvsdir_isboolean (dir, name) ? "boolean" : "JSON");
        }
    }
    cmb_kvsitr_destroy (itr);
}

void dump_all (kvsdir_t dir, bool ropt)
{
    kvsitr_t itr = cmb_kvsitr_create (dir);
    const char *name;
    char *key;

    while ((name = cmb_kvsitr_next (itr))) {
        key = cmb_kvsdir_key_at (dir, name);
        if (cmb_kvsdir_isdir (dir, name)) {
            if (ropt) {
                kvsdir_t ndir;
                if (cmb_kvs_get_dir_at (dir, name, &ndir) < 0)
                    err_exit ("cmb_kvs_get_dir_at %s", key);
                dump_all (ndir, ropt); 
                cmb_kvsdir_destroy (ndir);
            } else
                printf ("%s{%s}\n", key, "dir");
        } else if (cmb_kvsdir_isstring (dir, name)) {
            char *s;
            if (cmb_kvs_get_string_at (dir, name, &s) < 0)
                err_exit ("cmb_kvs_get_string_at %s", key);
            printf ("%s = %s\n", key, s);
            free (s);
        } else if (cmb_kvsdir_isint (dir, name)) {
            int i;
            if (cmb_kvs_get_int_at (dir, name, &i) < 0)
                err_exit ("cmb_kvs_get_int64_at %s", key);
            printf ("%s = %d\n", key, i);
        } else if (cmb_kvsdir_isint64 (dir, name)) {
            int64_t i;
            if (cmb_kvs_get_int64_at (dir, name, &i) < 0)
                err_exit ("cmb_kvs_get_int64_at %s", key);
            printf ("%s = %lld\n", key, (long long int)i);
        } else if (cmb_kvsdir_isdouble (dir, name)) {
            double n;
            if (cmb_kvs_get_double_at (dir, name, &n) < 0)
                err_exit ("cmb_kvs_get_double_at %s", key);
            printf ("%s = %lf\n", key, n);
        } else if (cmb_kvsdir_isboolean (dir, name)) {
            bool b;
            if (cmb_kvs_get_boolean_at (dir, name, &b) < 0)
                err_exit ("cmb_kvs_get_boolean_at %s", key);
            printf ("%s = %s\n", key, b ? "true" : "false");
        } else {
            json_object *o;
            if (cmb_kvs_get_at (dir, name, &o) < 0)
                err_exit ("cmb_kvs_get_object_at %s", key);
            printf ("%s = %s\n", key, json_object_to_json_string (o));
            json_object_put (o);
        }
        free (key);
    }
    cmb_kvsitr_destroy (itr);
}

void kvs_get_dir (cmb_t c, char *key, bool watch, bool ropt, bool all)
{
    int flags = 0;
    kvsdir_t dir;

    if (watch)
        flags = KVS_GET_WATCH;
    do {
        if (cmb_kvs_get_dir (c, key, &dir, flags) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else
                err_exit ("cmb_kvs_get %s", key);
        } else {
            if (all)
                dump_all (dir, ropt);
            else 
                dump_dir (dir, ropt);
            cmb_kvsdir_destroy (dir);
        }
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void kvs_get_string (cmb_t c, char *key, bool watch)
{
    char *val;
    int flags = 0;

    if (watch)
        flags = KVS_GET_WATCH;
    do {
        if (cmb_kvs_get_string (c, key, &val, flags) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else
                err_exit ("cmb_kvs_get_string %s", key);
        } else {
            printf ("%s\n", val);
            free (val);
        }
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void kvs_put_string (cmb_t c, char *key, char *val)
{
    if (cmb_kvs_put_string (c, key, val) < 0)
        err_exit ("cmb_kvs_put_string %s=%s", key, val);
}

void kvs_get_int (cmb_t c, char *key, bool watch)
{
    int val;
    int flags = 0;

    if (watch)
        flags = KVS_GET_WATCH;
    do {
        if (cmb_kvs_get_int (c, key, &val, flags) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else
                err_exit ("cmb_kvs_get_int %s", key);
        } else
            printf ("%d\n", val);
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void kvs_put_int (cmb_t c, char *key, int val)
{
    if (cmb_kvs_put_int (c, key, val) < 0)
        err_exit ("cmb_kvs_put_int %s=%d", key, val);
}

void kvs_get_int64 (cmb_t c, char *key, bool watch)
{
    int64_t val;
    int flags = 0;

    if (watch)
        flags = KVS_GET_WATCH;
    do {
        if (cmb_kvs_get_int64 (c, key, &val, flags) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else 
                err_exit ("cmb_kvs_get_int64 %s", key);
        } else
            printf ("%lld\n", (long long int) val);
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void kvs_put_int64 (cmb_t c, char *key, int64_t val)
{
    if (cmb_kvs_put_int64 (c, key, val) < 0)
        err_exit ("cmb_kvs_put_int64 %s=%lld", key, (long long int)val);
}

void kvs_get_double  (cmb_t c, char *key, bool watch)
{
    double val;
    int flags = 0;

    if (watch)
        flags = KVS_GET_WATCH;
    do {
        if (cmb_kvs_get_double (c, key, &val, flags) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else
                err_exit ("cmb_kvs_get_double  %s", key);
        } else
            printf ("%f\n", val);
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void kvs_put_double (cmb_t c, char *key, double val)
{
    if (cmb_kvs_put_double (c, key, val) < 0)
        err_exit ("cmb_kvs_put_double %s=%lf", key, val);
}

void kvs_get_boolean (cmb_t c, char *key, bool watch)
{
    bool val;
    int flags = 0;

    if (watch)
        flags = KVS_GET_WATCH;
    do { 
        if (cmb_kvs_get_boolean (c, key, &val, 0) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else
                err_exit ("cmb_kvs_get_boolean %s", key);
        } else
            printf ("%s\n", val ? "true" : "false");
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void kvs_put_boolean (cmb_t c, char *key, bool val)
{
    if (cmb_kvs_put_boolean (c, key, val) < 0)
        err_exit ("cmb_kvs_put_boolean %s=%s", key, val ? "true" : "false");
}
void kvs_commit (cmb_t c)
{
    if (cmb_kvs_commit (c) < 0)
        err_exit ("cmb_kvs_commit");
}

int main (int argc, char *argv[])
{
    int ch;
    bool wopt = false;
    char *op = NULL;
    char *key = NULL;
    char *val = NULL;
    cmb_t c;
    char path[PATH_MAX + 1];
    int flags = 0;

    log_init (basename (argv[0]));
    snprintf (path, sizeof (path), CMB_API_PATH_TMPL, getuid ());
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'Z': /* --trace-apisock */
                flags |= CMB_FLAGS_TRACE;
                break;
            case 'w': /* --watch */
                wopt = true;
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
    if (!op || (!strncmp (op, "get", 3) && !key)
            || (!strcmp (op, "unlink") && !key)
            || (!strcmp (op, "mkdir") && !key)
            || (!strncmp (op, "put", 3) && (!key || !val)))
        usage ();
    if (!(c = cmb_init_full (path, flags)))
        err_exit ("cmb_init");

    if (!strcmp (op, "get_string"))
        kvs_get_string (c, key, wopt);
    else if (!strcmp (op, "put_string"))
        kvs_put_string (c, key, val);

    else if (!strcmp (op, "get_int"))
        kvs_get_int (c, key, wopt);
    else if (!strcmp (op, "put_int"))
        kvs_put_int (c, key, strtoul (val, NULL, 10));

    else if (!strcmp (op, "get_int64"))
        kvs_get_int64 (c, key, wopt);
    else if (!strcmp (op, "put_int64"))
        kvs_put_int64 (c, key, strtoull (val, NULL, 10));

    else if (!strcmp (op, "get_double"))
        kvs_get_double (c, key, wopt);
    else if (!strcmp (op, "put_double"))
        kvs_put_double (c, key, strtod (val, NULL));

    else if (!strcmp (op, "get_boolean"))
        kvs_get_boolean (c, key, wopt);
    else if (!strcmp (op, "put_boolean"))
        kvs_put_boolean (c, key, !strcmp (val, "false") ? false : true);

    else if (!strcmp (op, "get_dir"))
        kvs_get_dir (c, key, wopt, false, false);
    else if (!strcmp (op, "get_dir_r"))
        kvs_get_dir (c, key, wopt, true, false);

    else if (!strcmp (op, "get_all"))
        kvs_get_dir (c, key, wopt, false, true);
    else if (!strcmp (op, "get_all_r"))
        kvs_get_dir (c, key, wopt, true, true);

    else if (!strcmp (op, "get"))
        kvs_get (c, key, wopt);
    else if (!strcmp (op, "put"))
        kvs_put (c, key, val);

    else if (!strcmp (op, "unlink"))
        kvs_unlink (c, key);
    else if (!strcmp (op, "mkdir"))
        kvs_mkdir (c, key);

    else if (!strcmp (op, "commit"))
        kvs_commit (c);     
    else
        usage ();
    cmb_fini (c);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

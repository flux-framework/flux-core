/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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

#include <jansson.h>
#include <systemd/sd-bus.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

#include "message.h"

/* Message diag is not proper TAP output so set to 0 except during development.
 */
#define ENABLE_MESSAGE_DIAG 0

void diagjson (json_t *o)
{
    char *s = json_dumps (o, JSON_COMPACT);
    diag ("%s", s ? s : "(null)");
    free (s);
}

void diagmsg (sd_bus_message *m)
{
#if defined (HAVE_SD_BUS_MESSAGE_DUMP) && ENABLE_MESSAGE_DIAG
    (void)sd_bus_message_rewind (m, true);
    (void)sd_bus_message_dump (m, stderr, 0);
    (void)sd_bus_message_rewind (m, true);
#endif
}

void msgtype_is (sd_bus_message *m, const char *fmt)
{
    char type[65] = "";

    if (m) {
        (void)sd_bus_message_rewind (m, true);
        for (int i = 0; i < sizeof (type) - 1; i++) {
            if (sd_bus_message_peek_type (m, &type[i], NULL) < 1
                || sd_bus_message_skip (m, &type[i]) < 0)
                break;
        }
        (void)sd_bus_message_rewind (m, true);
    }
    bool match = streq (type, fmt);
    ok (match, "message type has %s signature", fmt);
    if (!match)
        diag ("message type %s != %s signature", type, fmt);
}

void test_typestr (sd_bus *bus)
{
    const char *s;
    sd_bus_message *m;

    s = sdmsg_typestr (NULL);
    ok (s && streq (s, "unknown"),
        "sdmsg_typestr m=NULL returns 'unknown'");

    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_CALL) < 0)
        BAIL_OUT ("could not create method call message");
    s = sdmsg_typestr (m);
    ok (s && streq (s, "method-call"),
        "sdmsg_typestr m=method call returns 'method-call'");
    sd_bus_message_unref (m);

    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_RETURN) < 0)
        BAIL_OUT ("could not create method return message");
    s = sdmsg_typestr (m);
    ok (s && streq (s, "method-return"),
        "sdmsg_typestr m=method return returns 'method-return'");
    sd_bus_message_unref (m);

    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_ERROR) < 0)
        BAIL_OUT ("could not create method errr message");
    s = sdmsg_typestr (m);
    ok (s && streq (s, "method-error"),
        "sdmsg_typestr m=method return returns 'method-error'");
    sd_bus_message_unref (m);

    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_SIGNAL) < 0)
        BAIL_OUT ("could not create signal message");
    s = sdmsg_typestr (m);
    ok (s && streq (s, "signal"),
        "sdmsg_typestr m=signal returns 'signal'");
    sd_bus_message_unref (m);
}

/* Check that an object containing all of the basic D-Bus types can be
 * converted from json->dbus->json.  The input and output json objects
 * are compared for equality.
 */
void test_basic (sd_bus *bus)
{
    json_t *o;
    json_t *o2;
    int rc;
    sd_bus_message *m;

    if (!(o = json_pack ("[ibiiiiiifsss]",
                         42,
                         true,
                         -30000,
                         48000,
                         -100000,
                         100000,
                         -10,
                         10,
                         3.5,
                         "string",
                         "",
                         "/object/path/string.suffix")))
        BAIL_OUT ("could not pack json array");
    diagjson (o);
    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_CALL) < 0)
        BAIL_OUT ("could not create method call message");

    const char *fmt = "ybnqiuxtdsso";
    rc = sdmsg_write (m, fmt, o);
    ok (rc == 0,
        "sdmsg_write works");

    if (sd_bus_message_seal (m, 42, 0) < 0
        || sd_bus_message_rewind (m, true) < 0)
        BAIL_OUT ("could not finalize message");

    msgtype_is (m, fmt);

    diagmsg (m);

    if (!(o2 = json_array ()))
        BAIL_OUT ("could not create json array");
    rc = sdmsg_read (m, fmt, o2);
    diag ("sdmsg_read returned %d", rc);
    ok (rc == 1,
        "sdmsg_read works");
    diagjson (o2);
    ok (sd_bus_message_at_end (m, true),
        "all message contents were read");
    ok (json_equal (o, o2),
        "json in/out are the same");

    json_decref (o2);
    sd_bus_message_unref (m);
    json_decref (o);
}

/* Check that a struct containing string, array-of-string, and boolean "(sasb)"
 * can be converted from json->dbus.  dbus->json is not supported yet so
 * use sd_bus_message accessors to check that dbus content is correct.
 * N.B. An array of (sasb) is required in the StartTransientUnit request.
 */
void test_struct_sasb (sd_bus *bus)
{
    json_t *o;
    const char *fmt = "(sasb)";
    sd_bus_message *m;
    const char *s;
    int b;

    if (!(o = json_pack ("[s[ss]b]", "foo", "a1", "a2", 1)))
        BAIL_OUT ("could not pack json array for (sasb)");
    diagjson (o);

    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_CALL) < 0)
        BAIL_OUT ("could not create method call message");

    ok (sdmsg_put (m, fmt, o) == 0,
        "sdmsg_put works with struct (sasb)");

    if (sd_bus_message_seal (m, 42, 0) < 0
        || sd_bus_message_rewind (m, true) < 0)
        BAIL_OUT ("could not finalize message");

    diagmsg (m);

    if (sd_bus_message_enter_container (m, 'r', "sasb") <= 0)
        BAIL_OUT ("could not enter struct container");

    ok (sd_bus_message_read (m, "s", &s) > 0
        && streq (s, "foo"),
        "successfully read back first (string) element");

    ok (sd_bus_message_enter_container (m, 'a', "s") > 0
        && sd_bus_message_read (m, "s", &s) > 0
        && streq (s, "a1")
        && sd_bus_message_read (m, "s", &s) > 0
        && streq (s, "a2")
        && sd_bus_message_exit_container (m) > 0,
        "successfully read back second (array) element");

    ok (sd_bus_message_read (m, "b", &b) > 0
        && b == 1,
        "successfully read back third (boolean) element");

    if (sd_bus_message_exit_container (m) <= 0)
        BAIL_OUT ("error exiting struct container");

    sd_bus_message_unref (m);
    json_decref (o);
}

/* Convert three variants (integer, string, float) from json->dbus->json.
 * The input and output json objects are compared for equality.
 */
void test_variant (sd_bus *bus)
{
    json_t *o;
    json_t *o2;
    sd_bus_message *m;
    int rc;

    if (!(o = json_pack ("[[si][ss][sf]]",
                         "i", 42,
                         "s", "fubar",
                         "d", -1.5)))
        BAIL_OUT ("could not pack json array");
    diagjson (o);

    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_CALL) < 0)
        BAIL_OUT ("could not create method call message");

    const char *fmt = "vvv";
    rc = sdmsg_write (m, fmt, o);
    ok (rc == 0,
        "sdmsg_write works with variants");

    if (sd_bus_message_seal (m, 42, 0) < 0
        || sd_bus_message_rewind (m, true) < 0)
        BAIL_OUT ("could not finalize message");

    msgtype_is (m, "vvv");

    diagmsg (m);

    if (!(o2 = json_array ()))
        BAIL_OUT ("could not create json array");
    rc = sdmsg_read (m, fmt, o2);
    diag ("sdmsg_read returned %d", rc);
    ok (rc == 1,
        "sdmsg_read works");
    diagjson (o2);
    ok (sd_bus_message_at_end (m, true),
        "all message contents were read");
    ok (json_equal (o, o2),
        "json in/out are the same");

    json_decref (o2);
    sd_bus_message_unref (m);
    json_decref (o);
}

/* Convert an array-of-string from json->dbus->json.
 * The input and output json objects are compared for equality.
 */
void test_variant_as (sd_bus *bus)
{
    sd_bus_message *m;
    json_t *in;
    json_t *out;
    const char *fmt = "v";
    int rc;

    if (!(in = json_pack ("[[s[sss]]]", "as", "foo", "bar", "baz")))
        BAIL_OUT ("could not create json object");
    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_RETURN) < 0)
        BAIL_OUT ("could not create message");
    ok (sdmsg_write (m, fmt, in) == 0,
        "sdmsg_write of variant string array works");

    if (sd_bus_message_seal (m, 42, 0) < 0
        || sd_bus_message_rewind (m, true) < 0)
        BAIL_OUT ("could not finalize message");
    diagmsg (m);
    msgtype_is (m, fmt);

    if (!(out = json_array ()))
        BAIL_OUT ("could not create json array");

    rc = sdmsg_read (m, fmt, out);
    diag ("sdmsg_read returned %d", rc);
    ok (rc == 1,
        "sdmsg_read works on message containing string array variant");

    diagjson (out);

    ok (json_equal (in, out),
        "json in/out are the same");

    json_decref (out);
    json_decref (in);
    sd_bus_message_unref (m);
}

/* In property dicts (e.g. GetAll) we don't know how to decode all values yet.
 * It seems most sane to decode keys with a JSON null value rather than omit
 * those keys.  Create an sdbus message containing complex variants, then
 * convert dbus->json.  Verify that values that can't be decoded are null.
 */
void test_variant_unknown (sd_bus *bus)
{
    sd_bus_message *m;
    json_t *o;
    const char *fmt = "svs";
    uint8_t y[2] = { 99, 100 };
    int rc;

    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_CALL) < 0
        || sd_bus_message_append (m, "s", "eek") < 0
        || sd_bus_message_open_container (m, 'v', "a(yy)") < 0
        || sd_bus_message_open_container (m, 'a', "(yy)") < 0
        || sd_bus_message_open_container (m, 'r', "yy") < 0
        || sd_bus_message_append (m, "yy", y[0], y[1]) < 0
        || sd_bus_message_close_container (m) < 0
        || sd_bus_message_close_container (m) < 0
        || sd_bus_message_close_container (m) < 0
        || sd_bus_message_append (m, "s", "ook") < 0
        || sd_bus_message_seal (m, 42, 0) < 0
        || sd_bus_message_rewind (m, true) < 0)
        BAIL_OUT ("could not create message containing complex variant");
    diagmsg (m);
    msgtype_is (m, fmt);
    if (!(o = json_array ()))
        BAIL_OUT ("could not create json array");

    rc = sdmsg_read (m, fmt, o);
    diag ("sdmsg_read returned %d", rc);
    ok (rc == 1,
        "sdmsg_read works on message containing complex variant");

    diagjson (o);

    const char *s1, *s2, *type;
    ok (json_unpack (o, "[s[sn]s]", &s1, &type, &s2) == 0
        && streq (s1, "eek")
        && streq (type, "a(yy)")
        && streq (s2, "ook"),
        "complex variant was translated to json null");

    json_decref (o);
    sd_bus_message_unref (m);
}

/* StartTransientUnit wants a property array rather than the D-bus std dict.
 * Create one and convert json->dbus.  Then since we don't require the reverse
 * encoding, use sd_bus_message accessors to verify the result.
 */
void test_property_array (sd_bus *bus)
{
    json_t *o;
    json_error_t error;
    sd_bus_message *m;
    const char *fmt = "a(sv)";
    const char *key;
    const char *s;
    const char *s2;
    int b;

    if (!(o = json_pack_ex (&error,
                            0,
                            "["
                            "[s[ss]]"
                            "[s[sb]]"
                            "[s[s[ss]]]"
                            "[s[s[[s[ss]b]]]]"
                            "]",
                            "key1", "s", "val1",
                            "key2", "b", 1,
                            "key3", "as", "a1", "a2",
                            "key4", "a(sasb)", "foo", "a1", "a2", 0)))
        BAIL_OUT ("error creating properties object: %s", error.text);
    diagjson (o);

    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_CALL) < 0)
        BAIL_OUT ("could not create message");
    ok (sdmsg_put (m, fmt, o) == 0,
        "sdmsg_put of property array works");
    if (sd_bus_message_seal (m, 42, 0) < 0
        || sd_bus_message_rewind (m, true) < 0)
        BAIL_OUT ("could not finalize message");
    diagmsg (m);

    if (sd_bus_message_enter_container (m, 'a', "(sv)") <= 0)
        BAIL_OUT ("could not enter property array container");

    ok (sd_bus_message_enter_container (m, 'r', "sv") > 0
        && sd_bus_message_read (m, "s", &key) > 0
        && sd_bus_message_enter_container (m, 'v', "s") > 0
        && sd_bus_message_read (m, "s", &s) > 0
        && sd_bus_message_exit_container (m) > 0
        && sd_bus_message_exit_container (m) > 0
        && streq (key, "key1")
        && streq (s, "val1"),
        "successfully read back first property");

    ok (sd_bus_message_enter_container (m, 'r', "sv") > 0
        && sd_bus_message_read (m, "s", &key) > 0
        && sd_bus_message_enter_container (m, 'v', "b") > 0
        && sd_bus_message_read (m, "b", &b) > 0
        && sd_bus_message_exit_container (m) > 0
        && sd_bus_message_exit_container (m) > 0
        && streq (key, "key2")
        && b == 1,
        "successfully read back second property");

    ok (sd_bus_message_enter_container (m, 'r', "sv") > 0
        && sd_bus_message_read (m, "s", &key) > 0
        && sd_bus_message_enter_container (m, 'v', "as") > 0
        && sd_bus_message_enter_container (m, 'a', "s") > 0
        && sd_bus_message_read (m, "s", &s) > 0
        && sd_bus_message_read (m, "s", &s2) > 0
        && sd_bus_message_exit_container (m) > 0
        && sd_bus_message_exit_container (m) > 0
        && sd_bus_message_exit_container (m) > 0
        && streq (key, "key3")
        && streq (s, "a1")
        && streq (s2, "a2"),
        "successfully read back third property");

    ok (sd_bus_message_enter_container (m, 'r', "sv") > 0
        && sd_bus_message_read (m, "s", &key) > 0
        && streq (key, "key4")
        && sd_bus_message_enter_container (m, 'v', "a(sasb)") > 0
        && sd_bus_message_enter_container (m, 'a', "(sasb)") > 0
        && sd_bus_message_enter_container (m, 'r', "sasb") > 0
        && sd_bus_message_read (m, "s", &s) > 0
        && streq (s, "foo")
        && sd_bus_message_enter_container (m, 'a', "s") > 0
        && sd_bus_message_read (m, "s", &s) > 0
        && streq (s, "a1")
        && sd_bus_message_read (m, "s", &s) > 0
        && streq (s, "a2")
        && sd_bus_message_exit_container (m) > 0
        && sd_bus_message_read (m, "b", &b) > 0
        && b == 0
        && sd_bus_message_exit_container (m) > 0
        && sd_bus_message_exit_container (m) > 0
        && sd_bus_message_exit_container (m) > 0,
        "successfully read back fourth property");

    if (sd_bus_message_exit_container (m) <= 0)
        BAIL_OUT ("error exiting property array container");

    json_decref (o);
    sd_bus_message_unref (m);
}

int main (int argc, char **argv)
{
    sd_bus *bus;
    int e;

    plan (NO_PLAN);

    if ((e = sd_bus_open_user (&bus)) < 0) {
        diag ("could not open sdbus: %s", strerror (e));
        if (!getenv ("DBUS_SESSION_BUS_ADDRESS"))
            diag ("Hint: DBUS_SESSION_BUS_ADDRESS is not set");
        if (!getenv ("XDG_RUNTIME_DIR"))
            diag ("Hint: XDG_RUNTIME_DIR is not set");
        plan (SKIP_ALL);
        done_testing ();
    }

    test_typestr (bus);
    test_basic (bus);
    test_struct_sasb (bus);
    test_variant (bus);
    test_variant_as (bus);
    test_variant_unknown (bus);
    test_property_array (bus);

    sd_bus_flush (bus);
    sd_bus_close (bus);
    sd_bus_unref (bus);

    done_testing ();
}

// vi: ts=4 sw=4 expandtab

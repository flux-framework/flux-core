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

/* Finalize message 'm' so it can be read.
 */
static void seal (sd_bus_message *m)
{
    if (sd_bus_message_seal (m, 42, 0) < 0
        || sd_bus_message_rewind (m, true) < 0)
        BAIL_OUT ("could not finalize message");
}

/* Convert 'in' (JSON text) to a D-Bus message with signature 'sig',
 * then back to JSON.  Return true if the message has signature 'sig' and
 * the result equals 'out' (JSON text), or 'in' if 'out' is NULL.
 */
static bool roundtrip_ex (sd_bus *bus,
                          const char *sig,
                          const char *in,
                          const char *out)
{
    json_t *o_in = NULL;
    json_t *o_out = NULL;
    json_t *o_expect = NULL;
    json_error_t error;
    sd_bus_message *m;
    const char *msig;
    bool result = false;
    int e;

    if (!(o_in = json_loads (in, 0, &error))
        || !(o_expect = json_loads (out ? out : in, 0, &error))
        || !(o_out = json_array ()))
        BAIL_OUT ("could not parse test JSON: %s", error.text);
    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_CALL) < 0)
        BAIL_OUT ("could not create method call message");
    if ((e = sdmsg_write (m, sig, o_in)) < 0) {
        diag ("sdmsg_write %s: %s", sig, strerror (-e));
        goto done;
    }
    seal (m);
    diagmsg (m);
    if (!(msig = sd_bus_message_get_signature (m, true))
        || !streq (msig, sig)) {
        diag ("message signature %s != %s", msig ? msig : "(null)", sig);
        goto done;
    }
    if ((e = sdmsg_read (m, o_out)) < 0) {
        diag ("sdmsg_read %s: %s", sig, strerror (-e));
        goto done;
    }
    if (!sd_bus_message_at_end (m, true)) {
        diag ("message was not completely read");
        goto done;
    }
    if (!json_equal (o_expect, o_out)) {
        diagjson (o_out);
        goto done;
    }
    result = true;
done:
    sd_bus_message_unref (m);
    json_decref (o_out);
    json_decref (o_expect);
    json_decref (o_in);
    return result;
}

static bool roundtrip (sd_bus *bus, const char *sig, const char *in)
{
    return roundtrip_ex (bus, sig, in, NULL);
}

/* Return the error from converting 'in' (JSON text) to a D-Bus message
 * with signature 'sig', or 0 if it succeeds.
 */
static int write_error (sd_bus *bus, const char *sig, const char *in)
{
    json_t *o;
    json_error_t error;
    sd_bus_message *m;
    int e;

    if (!(o = json_loads (in, JSON_DECODE_ANY | JSON_ALLOW_NUL, &error)))
        BAIL_OUT ("could not parse test JSON: %s", error.text);
    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_CALL) < 0)
        BAIL_OUT ("could not create method call message");
    e = sdmsg_write (m, sig, o);
    sd_bus_message_unref (m);
    json_decref (o);
    return e < 0 ? e : 0;
}

/* Build a D-Bus message with a(yy) inside a variant, plus a string on
 * each side, as sd-bus would deliver it, then convert it to JSON.
 */
void test_read_complex_variant (sd_bus *bus)
{
    sd_bus_message *m;
    json_t *o;
    json_t *expect;

    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_CALL) < 0
        || sd_bus_message_append (m, "s", "eek") < 0
        || sd_bus_message_open_container (m, 'v', "a(yy)") < 0
        || sd_bus_message_open_container (m, 'a', "(yy)") < 0
        || sd_bus_message_open_container (m, 'r', "yy") < 0
        || sd_bus_message_append (m, "yy", 99, 100) < 0
        || sd_bus_message_close_container (m) < 0
        || sd_bus_message_close_container (m) < 0
        || sd_bus_message_close_container (m) < 0
        || sd_bus_message_append (m, "s", "ook") < 0)
        BAIL_OUT ("could not create message containing complex variant");
    seal (m);
    diagmsg (m);
    if (!(o = json_array ())
        || !(expect = json_loads ("[\"eek\",[\"a(yy)\",[[99,100]]],\"ook\"]",
                                  0,
                                  NULL)))
        BAIL_OUT ("could not create json objects");
    ok (sdmsg_read (m, o) == 0 && json_equal (o, expect),
        "sdmsg_read decodes a(yy) variant");
    diagjson (o);
    json_decref (expect);
    json_decref (o);
    sd_bus_message_unref (m);
}

void test_basic (sd_bus *bus)
{
    ok (roundtrip (bus,
                   "ybnqiuxtdsgo",
                   "[42,true,-30000,48000,-100000,100000,\"-10\",\"10\",3.5,"
                   "\"string\",\"a{sv}\",\"/object/path/string.suffix\"]"),
        "basic types round trip");
    ok (roundtrip (bus, "ss", "[\"\",\"\"]"),
        "empty strings round trip");
    ok (roundtrip (bus, "", "[]"),
        "empty body round trips");
    ok (roundtrip (bus,
                   "ynqiu",
                   "[255,32767,65535,2147483647,4294967295]"),
        "maximum integer values round trip");
    ok (roundtrip (bus,
                   "ynqiu",
                   "[0,-32768,0,-2147483648,0]"),
        "minimum integer values round trip");
    ok (roundtrip_ex (bus, "d", "[1]", "[1.0]"),
        "integer is accepted for d");
}

void test_int64 (sd_bus *bus)
{
    ok (roundtrip (bus, "tt", "[\"0\",\"18446744073709551615\"]"),
        "t limits round trip as strings");
    ok (roundtrip (bus, "xxx",
                   "[\"-9223372036854775808\",\"0\",\"9223372036854775807\"]"),
        "x limits round trip as strings");
    ok (roundtrip (bus, "v", "[[\"t\",\"9007199254740993\"]]"),
        "t above 2^53 round trips exactly");

    ok (write_error (bus, "t", "[42]") == -EPROTO,
        "integer for t fails with EPROTO");
    ok (write_error (bus, "x", "[-1]") == -EPROTO,
        "integer for x fails with EPROTO");
    ok (write_error (bus, "t", "[\"18446744073709551616\"]") == -EPROTO,
        "t overflow fails with EPROTO");
    ok (write_error (bus, "x", "[\"9223372036854775808\"]") == -EPROTO,
        "x overflow fails with EPROTO");
    ok (write_error (bus, "x", "[\"-9223372036854775809\"]") == -EPROTO,
        "x underflow fails with EPROTO");
    ok (write_error (bus, "t", "[\"-1\"]") == -EPROTO,
        "negative t fails with EPROTO");
    ok (write_error (bus, "t", "[\"007\"]") == -EPROTO,
        "t with leading zeros fails with EPROTO");
    ok (write_error (bus, "x", "[\"+5\"]") == -EPROTO,
        "x with leading + fails with EPROTO");
    ok (write_error (bus, "x", "[\"-0\"]") == -EPROTO,
        "x of -0 fails with EPROTO");
    ok (write_error (bus, "t", "[\" 5\"]") == -EPROTO,
        "t with leading whitespace fails with EPROTO");
    ok (write_error (bus, "t", "[\"5 \"]") == -EPROTO,
        "t with trailing whitespace fails with EPROTO");
    ok (write_error (bus, "t", "[\"\"]") == -EPROTO,
        "empty t fails with EPROTO");
    ok (write_error (bus, "t", "[\"0x10\"]") == -EPROTO,
        "hex t fails with EPROTO");
}

void test_containers (sd_bus *bus)
{
    ok (roundtrip (bus, "(sasb)", "[[\"foo\",[\"a1\",\"a2\"],true]]"),
        "struct (sasb) round trips");
    ok (roundtrip (bus, "ai", "[[1,2,3]]"),
        "array of int32 round trips");
    ok (roundtrip (bus, "ay", "[[]]"),
        "empty array round trips");
    ok (roundtrip (bus, "aas", "[[[\"a\"],[],[\"b\",\"c\"]]]"),
        "array of array round trips");
    ok (roundtrip (bus, "a(ss)",
                   "[[[\"/dev/nvidiactl\",\"rw\"],[\"/dev/nvidia0\",\"r\"]]]"),
        "a(ss) round trips");
    ok (roundtrip (bus, "a{sv}",
                   "[{\"A\":[\"s\",\"x\"],\"B\":[\"u\",42]}]"),
        "a{sv} round trips");
    ok (roundtrip (bus, "a{sa{sv}}",
                   "[{\"eth0\":{\"mtu\":[\"u\",1500]},\"lo\":{}}]"),
        "nested dict round trips");
    ok (roundtrip (bus, "a{ss}", "[{}]"),
        "empty dict round trips");
}

void test_variants (sd_bus *bus)
{
    ok (roundtrip (bus, "vvv",
                   "[[\"i\",42],[\"s\",\"fubar\"],[\"d\",-1.5]]"),
        "basic variants round trip");
    ok (roundtrip (bus, "v", "[[\"as\",[\"foo\",\"bar\",\"baz\"]]]"),
        "as variant round trips");
    ok (roundtrip (bus, "v", "[[\"a(ss)\",[[\"/dev/null\",\"rw\"]]]]"),
        "a(ss) variant round trips");
    ok (roundtrip (bus, "v", "[[\"v\",[\"v\",[\"b\",false]]]]"),
        "nested variants round trip");
    ok (roundtrip (bus, "av", "[[[\"i\",1],[\"(ss)\",[\"a\",\"b\"]]]]"),
        "array of variants round trips");
}

/* Signatures from systemd methods and properties used by Flux.
 */
void test_systemd (sd_bus *bus)
{
    ok (roundtrip (bus, "a(sv)",
                   "[["
                   "[\"key1\",[\"s\",\"val1\"]],"
                   "[\"key2\",[\"b\",true]],"
                   "[\"key3\",[\"as\",[\"a1\",\"a2\"]]],"
                   "[\"key4\",[\"a(sasb)\",[[\"foo\",[\"a1\",\"a2\"],false]]]],"
                   "[\"DeviceAllow\",[\"a(ss)\",[[\"/dev/nvidia0\",\"r\"]]]]"
                   "]]"),
        "StartTransientUnit property array round trips");
    ok (roundtrip (bus, "ssa(sv)a(sa(sv))",
                   "[\"foo.service\",\"fail\","
                   "[[\"Description\",[\"s\",\"hi\"]]],"
                   "[[\"aux.service\",[[\"RemainAfterExit\",[\"b\",true]]]]]]"),
        "StartTransientUnit arguments with aux units round trip");
    ok (roundtrip (bus, "ssa(sv)a(sa(sv))",
                   "[\"foo.service\",\"fail\",[],[]]"),
        "StartTransientUnit arguments without aux units round trip");
    ok (roundtrip (bus, "a(ssssssouso)",
                   "[[[\"a.service\",\"desc\",\"loaded\",\"active\","
                   "\"running\",\"\",\"/org/x/a\",0,\"\",\"/\"]]]"),
        "ListUnitsByPatterns reply round trips");
    ok (roundtrip (bus, "sa{sv}as",
                   "[\"org.freedesktop.systemd1.Service\","
                   "{\"MainPID\":[\"u\",4242],"
                   "\"ExecStart\":[\"a(sasbttttuii)\","
                   "[[\"/bin/sleep\",[\"sleep\",\"60\"],false,"
                   "\"1\",\"2\",\"3\",\"4\",4242,0,0]]]},"
                   "[]]"),
        "PropertiesChanged with ExecStart round trips");
}

void test_write_errors (sd_bus *bus)
{
    ok (write_error (bus, "ss", "[\"one\"]") == -EPROTO,
        "too few params fails with EPROTO");
    ok (write_error (bus, "s", "[\"one\",\"two\"]") == -EPROTO,
        "too many params fails with EPROTO");
    ok (write_error (bus, "s", "{}") == -EPROTO,
        "params that are not an array fails with EPROTO");
    ok (write_error (bus, "i", "[\"42\"]") == -EPROTO,
        "string for i fails with EPROTO");
    ok (write_error (bus, "s", "[42]") == -EPROTO,
        "integer for s fails with EPROTO");
    ok (write_error (bus, "b", "[1]") == -EPROTO,
        "integer for b fails with EPROTO");
    ok (write_error (bus, "i", "[1.5]") == -EPROTO,
        "real for i fails with EPROTO");
    ok (write_error (bus, "y", "[256]") == -EPROTO,
        "out of range y fails with EPROTO");
    ok (write_error (bus, "q", "[-1]") == -EPROTO,
        "out of range q fails with EPROTO");
    ok (write_error (bus, "u", "[4294967296]") == -EPROTO,
        "out of range u fails with EPROTO");
    ok (write_error (bus, "s", "[\"a\\u0000b\"]") == -EPROTO,
        "string with embedded NUL fails with EPROTO");
    ok (write_error (bus, "(si)", "[[\"x\"]]") == -EPROTO,
        "short struct fails with EPROTO");
    ok (write_error (bus, "as", "[\"x\"]") == -EPROTO,
        "string for as fails with EPROTO");
    ok (write_error (bus, "a{sv}", "[[]]") == -EPROTO,
        "array for a{sv} fails with EPROTO");
    ok (write_error (bus, "a{uv}", "[{}]") == -EPROTO,
        "dict with non-string key fails with EPROTO");
    ok (write_error (bus, "v", "[[\"(s\",[\"x\"]]]") == -EPROTO,
        "variant with bad signature fails with EPROTO");
    ok (write_error (bus, "v", "[[\"ss\",[\"x\",\"y\"]]]") == -EPROTO,
        "variant with multiple types fails with EPROTO");
    ok (write_error (bus, "v", "[[\"s\"]]") == -EPROTO,
        "variant without value fails with EPROTO");
    ok (write_error (bus, "a(", "[[]]") == -EPROTO,
        "bad signature fails with EPROTO");
    ok (write_error (bus, "()", "[[]]") == -EPROTO,
        "empty struct signature fails with EPROTO");
    ok (write_error (bus, "z", "[1]") == -EPROTO,
        "unknown type code fails with EPROTO");
    ok (write_error (bus, "g", "[\"(\"]") == -EPROTO,
        "invalid signature string fails with EPROTO");
    ok (write_error (bus, "g", "[\"{sv}\"]") == -EPROTO,
        "bare dict entry in g value fails with EPROTO");
    ok (write_error (bus, "{sv}", "[{}]") == -EPROTO,
        "bare dict entry signature fails with EPROTO");
    ok (write_error (bus, "a{vs}", "[[]]") == -EPROTO,
        "dict with non-basic key fails with EPROTO");
    ok (write_error (bus, "a{svs}", "[{}]") == -EPROTO,
        "dict entry with three members fails with EPROTO");
    ok (write_error (bus, "v", "[[\"s\",\"x\",\"y\"]]") == -EPROTO,
        "variant with extra element fails with EPROTO");
    ok (write_error (bus, "o", "[\"not/a/path\"]") < 0,
        "invalid object path fails");
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
    test_int64 (bus);
    test_containers (bus);
    test_variants (bus);
    test_systemd (bus);
    test_read_complex_variant (bus);
    test_write_errors (bus);

    sd_bus_flush (bus);
    sd_bus_close (bus);
    sd_bus_unref (bus);

    done_testing ();
}

// vi: ts=4 sw=4 expandtab

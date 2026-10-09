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
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <systemd/sd-bus.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

#include "message.h"
#include "rfc52_vectors.h"

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
                   "\"string\",\"a{sv}\",\"/object/path/string_2esuffix\"]"),
        "basic types round trip");
    ok (roundtrip (bus,
                   "o",
                   "[\"/org/freedesktop/systemd1/unit/foo_2eservice\"]"),
        "object path round trips verbatim");
    ok (write_error (bus,
                     "o",
                     "[\"/org/freedesktop/systemd1/unit/foo.service\"]")
        == -EPROTO,
        "object path with invalid character fails with EPROTO");
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

/* sd-bus duplicates a file descriptor when it is appended to a message,
 * so the number is not preserved.  Check that the descriptor in the
 * message refers to the same file as the one written.  Per RFC 52,
 * reading h back fails, and writing requires a matching pid.
 */
void test_unix_fd (sd_bus *bus)
{
    sd_bus_message *m;
    json_t *in;
    json_t *out;
    char buf[64];
    struct stat sb1, sb2;
    int fd;
    int fd2;

    if ((fd = open ("/dev/null", O_RDONLY)) < 0
        || fstat (fd, &sb1) < 0)
        BAIL_OUT ("could not open /dev/null");
    if (!(in = json_pack ("[{s:i s:i}]", "fd", fd, "pid", (int)getpid ()))
        || !(out = json_array ()))
        BAIL_OUT ("could not create json objects");
    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_CALL) < 0)
        BAIL_OUT ("could not create method call message");
    ok (sdmsg_write (m, "h", in) == 0,
        "sdmsg_write h with fd/pid object works");
    seal (m);
    ok (sd_bus_message_read_basic (m, 'h', &fd2) > 0
        && fd2 >= 0
        && fstat (fd2, &sb2) == 0
        && sb1.st_dev == sb2.st_dev
        && sb1.st_ino == sb2.st_ino,
        "message contains a descriptor for the same file");
    (void)sd_bus_message_rewind (m, true);
    ok (sdmsg_read (m, out) == -EPROTO,
        "sdmsg_read h fails with EPROTO");
    sd_bus_message_unref (m);
    json_decref (out);
    json_decref (in);
    close (fd);

    snprintf (buf,
              sizeof (buf),
              "[{\"fd\":0,\"pid\":%d}]",
              (int)getpid () + 1);
    ok (write_error (bus, "h", buf) == -ESRCH,
        "h with mismatched pid fails with ESRCH");
    snprintf (buf,
              sizeof (buf),
              "[{\"fd\":2147483648,\"pid\":%d}]",
              (int)getpid ());
    ok (write_error (bus, "h", buf) == -EPROTO,
        "out of range h fails with EPROTO");
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

void test_dict_pairs (sd_bus *bus)
{
    ok (roundtrip (bus, "a{uv}", "[[[7,[\"b\",true]],[3,[\"s\",\"x\"]]]]"),
        "a{uv} round trips as ordered pairs");
    ok (roundtrip (bus, "a{ts}", "[[[\"18446744073709551615\",\"max\"]]]"),
        "a{ts} round trips with string key");
    ok (roundtrip (bus, "a{bd}", "[[[true,1.5],[false,-1.5]]]"),
        "a{bd} round trips");
    ok (roundtrip (bus, "a{us}", "[[[1,\"a\"],[1,\"b\"]]]"),
        "a{us} with duplicate keys round trips");
    ok (roundtrip (bus, "a{yv}", "[[]]"),
        "empty a{yv} round trips");
    ok (roundtrip (bus, "a{ua{sv}}", "[[[1,{\"A\":[\"i\",1]}]]]"),
        "a{ua{sv}} round trips");

    ok (write_error (bus, "a{uv}", "[{}]") == -EPROTO,
        "object for a{uv} fails with EPROTO");
    ok (write_error (bus, "a{sv}", "[[]]") == -EPROTO,
        "array for a{sv} fails with EPROTO");
    ok (write_error (bus, "a{uv}", "[[[7]]]") == -EPROTO,
        "dict entry without value fails with EPROTO");
    ok (write_error (bus, "a{us}", "[[[7,\"a\",\"b\"]]]") == -EPROTO,
        "dict entry with extra value fails with EPROTO");
    ok (write_error (bus, "a{us}", "[[7]]") == -EPROTO,
        "dict entry that is not an array fails with EPROTO");
    ok (write_error (bus, "a{us}", "[[[\"7\",\"a\"]]]") == -EPROTO,
        "dict entry with wrong key type fails with EPROTO");
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
    ok (write_error (bus, "o", "[\"not/a/path\"]") == -EPROTO,
        "invalid object path fails with EPROTO");
}

/* Minimal parser for the busctl(1) parameter notation used by the RFC 52
 * test vectors: a signature followed by values, where an array is its
 * element count followed by its elements, a variant is its signature
 * followed by its value, and structs and dict entries are their members.
 * Tokens are separated by spaces; "" is the empty string.
 */
struct tokens {
    char *buf;
    char *next;
};

static const char *token_next (struct tokens *t)
{
    char *tok;

    while (*t->next == ' ')
        t->next++;
    if (*t->next == '\0')
        return NULL;
    tok = t->next;
    while (*t->next != '\0' && *t->next != ' ')
        t->next++;
    if (*t->next == ' ')
        *t->next++ = '\0';
    if (streq (tok, "\"\""))
        tok[0] = '\0';
    return tok;
}

static int nota_type_len (const char *sig)
{
    if (sig[0] == 'a')
        return 1 + nota_type_len (sig + 1);
    if (sig[0] == '(' || sig[0] == '{') {
        char close = sig[0] == '(' ? ')' : '}';
        int i = 1;
        while (sig[i] != close)
            i += nota_type_len (sig + i);
        return i + 1;
    }
    return 1;
}

static int nota_append (sd_bus_message *m,
                        const char *type,
                        int len,
                        struct tokens *t);

static int nota_append_basic (sd_bus_message *m, char type, const char *tok)
{
    union {
        uint8_t y; int b; int16_t n; uint16_t q; int32_t i; uint32_t u;
        int64_t x; uint64_t t; double d;
    } v;

    switch (type) {
        case 's':
        case 'o':
        case 'g':
            return sd_bus_message_append_basic (m, type, tok);
        case 'b':
            v.b = streq (tok, "true");
            break;
        case 'd':
            v.d = strtod (tok, NULL);
            break;
        case 'y': v.y = strtol (tok, NULL, 10); break;
        case 'n': v.n = strtol (tok, NULL, 10); break;
        case 'q': v.q = strtoul (tok, NULL, 10); break;
        case 'i': v.i = strtol (tok, NULL, 10); break;
        case 'u': v.u = strtoul (tok, NULL, 10); break;
        case 'x': v.x = strtoll (tok, NULL, 10); break;
        case 't': v.t = strtoull (tok, NULL, 10); break;
        case 'h': {
            // the token is ignored since a real descriptor is required
            int fd = open ("/dev/null", O_RDONLY);
            int e;
            if (fd < 0)
                return -errno;
            e = sd_bus_message_append_basic (m, type, &fd);
            close (fd); // sd-bus duplicated it
            return e;
        }
        default:
            return -EINVAL;
    }
    return sd_bus_message_append_basic (m, type, &v);
}

static int nota_append_members (sd_bus_message *m,
                                const char *sig,
                                int len,
                                struct tokens *t)
{
    int e;
    for (int i = 0; i < len; ) {
        int n = nota_type_len (sig + i);
        if ((e = nota_append (m, sig + i, n, t)) < 0)
            return e;
        i += n;
    }
    return 0;
}

static int nota_append (sd_bus_message *m,
                        const char *type,
                        int len,
                        struct tokens *t)
{
    const char *tok;
    char contents[256];
    int e;

    if (type[0] == 'a') {
        long count;
        snprintf (contents, sizeof (contents), "%.*s", len - 1, type + 1);
        if (!(tok = token_next (t)))
            return -EINVAL;
        count = strtol (tok, NULL, 10);
        if ((e = sd_bus_message_open_container (m, 'a', contents)) < 0)
            return e;
        for (long i = 0; i < count; i++) {
            if (type[1] == '{') {
                char entry[256];
                snprintf (entry, sizeof (entry), "%.*s", len - 3, type + 2);
                if ((e = sd_bus_message_open_container (m, 'e', entry)) < 0
                    || (e = nota_append_members (m, entry, len - 3, t)) < 0
                    || (e = sd_bus_message_close_container (m)) < 0)
                    return e;
            }
            else if ((e = nota_append (m, type + 1, len - 1, t)) < 0)
                return e;
        }
        return sd_bus_message_close_container (m);
    }
    if (type[0] == '(') {
        snprintf (contents, sizeof (contents), "%.*s", len - 2, type + 1);
        if ((e = sd_bus_message_open_container (m, 'r', contents)) < 0
            || (e = nota_append_members (m, contents, len - 2, t)) < 0)
            return e;
        return sd_bus_message_close_container (m);
    }
    if (!(tok = token_next (t)))
        return -EINVAL;
    if (type[0] == 'v') {
        snprintf (contents, sizeof (contents), "%s", tok);
        if ((e = sd_bus_message_open_container (m, 'v', contents)) < 0
            || (e = nota_append (m,
                                 contents,
                                 strlen (contents),
                                 t)) < 0)
            return e;
        return sd_bus_message_close_container (m);
    }
    return nota_append_basic (m, type[0], tok);
}

/* Create a sealed message from busctl notation 'body'.
 */
static sd_bus_message *nota_message (sd_bus *bus, const char *body)
{
    struct tokens t;
    const char *sig;
    sd_bus_message *m;

    if (sd_bus_message_new (bus, &m, SD_BUS_MESSAGE_METHOD_CALL) < 0
        || !(t.buf = strdup (body)))
        BAIL_OUT ("could not create message for %s", body);
    t.next = t.buf;
    if ((sig = token_next (&t))
        && nota_append_members (m, sig, strlen (sig), &t) < 0)
        BAIL_OUT ("could not build message for %s", body);
    if (token_next (&t))
        BAIL_OUT ("extra tokens in %s", body);
    free (t.buf);
    seal (m);
    return m;
}

/* RFC 52 Valid Bodies: encoding the D-Bus body produces params, and
 * decoding params with the body's signature produces the D-Bus body.
 * The message built from busctl notation is independent of sdmsg_write(),
 * and decoding is lossless, so a correct decode plus a JSON round trip
 * shows that encoding produces the same body.
 */
void test_rfc52_valid (sd_bus *bus)
{
    for (int i = 0; i < ARRAY_SIZE (valid_vectors); i++) {
        const struct valid_vector *v = &valid_vectors[i];
        sd_bus_message *m = nota_message (bus, v->body);
        const char *sig = sd_bus_message_get_signature (m, true);
        json_t *expect = json_loads (v->params, 0, NULL);
        json_t *o = json_array ();

        if (!expect || !o)
            BAIL_OUT ("could not create json objects");
        ok (sdmsg_read (m, o) == 0 && json_equal (o, expect),
            "RFC 52 vector %d decodes: %.40s", i, v->body);
        ok (roundtrip (bus, sig, v->params),
            "RFC 52 vector %d round trips: %.40s", i, v->params);
        json_decref (o);
        json_decref (expect);
        sd_bus_message_unref (m);
    }
}

/* RFC 52 Invalid Params: decoding params with signature fails.
 */
void test_rfc52_invalid_params (sd_bus *bus)
{
    for (int i = 0; i < ARRAY_SIZE (invalid_params_vectors); i++) {
        const struct invalid_params_vector *v = &invalid_params_vectors[i];
        ok (write_error (bus, v->signature, v->params) < 0,
            "RFC 52 invalid params %s %s fails (%s)",
            v->signature, v->params, v->reason);
    }
}

/* RFC 52 Invalid D-Bus Bodies: encoding the D-Bus body fails.
 */
void test_rfc52_invalid_body (sd_bus *bus)
{
    for (int i = 0; i < ARRAY_SIZE (invalid_body_vectors); i++) {
        const struct invalid_body_vector *v = &invalid_body_vectors[i];
        sd_bus_message *m = nota_message (bus, v->body);
        json_t *o = json_array ();

        if (!o)
            BAIL_OUT ("could not create json array");
        ok (sdmsg_read (m, o) < 0,
            "RFC 52 invalid body %s fails (%s)", v->body, v->reason);
        json_decref (o);
        sd_bus_message_unref (m);
    }
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
    test_unix_fd (bus);
    test_containers (bus);
    test_dict_pairs (bus);
    test_variants (bus);
    test_systemd (bus);
    test_read_complex_variant (bus);
    test_write_errors (bus);
    test_rfc52_valid (bus);
    test_rfc52_invalid_params (bus);
    test_rfc52_invalid_body (bus);

    sd_bus_flush (bus);
    sd_bus_close (bus);
    sd_bus_unref (bus);

    done_testing ();
}

// vi: ts=4 sw=4 expandtab

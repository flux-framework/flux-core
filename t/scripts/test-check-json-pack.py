#!/usr/bin/env python3
# Self-test for scripts/check-json-pack.
#
# Generates stub jansson + wrapper + libsdexec headers and good/bad fixture
# sources in a temporary directory, builds a compile_commands.json for them,
# runs the checker, and asserts it flags exactly the intended mismatches,
# for both the jansson pack/unpack family and the libsdexec D-Bus value
# readers.  Requires only clang (no flux build).
#
# The checker is located relative to this file (../../scripts/check-json-pack
# in the source tree).  If the environment variable CHECK_JSON_PACK_CLANG is
# set, its value is passed to the checker as the clang binary to use; this
# lets the sharness driver select a versioned clang (e.g. clang-15).

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CHECKER = os.path.normpath(os.path.join(HERE, "..", "..", "scripts", "check-json-pack"))

JANSSON_H = """\
#ifndef JANSSON_H
#define JANSSON_H
#include <stddef.h>
typedef struct json_t json_t;
typedef long long json_int_t;
typedef struct { int line; } json_error_t;
json_t *json_pack (const char *fmt, ...);
json_t *json_pack_ex (json_error_t *e, size_t flags, const char *fmt, ...);
int json_unpack (json_t *root, const char *fmt, ...);
int json_unpack_ex (json_t *root, json_error_t *e, size_t flags,
                    const char *fmt, ...);
#endif
"""

WRAP_H = """\
#ifndef WRAP_H
#define WRAP_H
#include "jansson.h"
int myx_pack (void *h, const char *fmt, ...);
int myx_unpack (void *h, const char *fmt, ...);
#endif
"""

SDEXEC_H = """\
#ifndef SDEXEC_H
#define SDEXEC_H
#include "jansson.h"
typedef struct flux_future flux_future_t;
struct sdexec_proplist;
int sdexec_value_read (json_t *val, const char *type, ...);
int sdexec_variant_read (json_t *val, const char *type, ...);
int sdexec_params_read (json_t *params, const char *sig, ...);
int sdexec_reply_read (flux_future_t *f, const char *sig, ...);
int sdexec_property_get_read (flux_future_t *f, const char *type, ...);
int sdexec_property_dict_read (json_t *dict,
                               const char *name,
                               const char *type,
                               ...);
void sdexec_proplist_add (struct sdexec_proplist *pl,
                          const char *name,
                          const char *type,
                          ...);
#endif
"""

# Every call here must type-check cleanly.
GOOD_C = """\
#include <stdint.h>
#include "wrap.h"
void good (void *h, json_t *root)
{
    int i = 0;
    json_int_t big = 0;
    int64_t big2 = 0;
    double d = 0;
    json_t *o = 0;
    const char *s = 0;
    size_t len = 0;

    json_pack ("{s:i}", "key", i);
    json_pack ("{s:I}", "key", big);
    json_pack ("{s:I}", "key", big2);
    json_pack ("{s:o}", "key", o);
    json_pack ("{s:O}", "key", root);
    json_pack ("{s:s}", "key", s);
    json_pack ("{s:s}", "key", "literal");
    json_pack ("{s:f}", "key", d);
    json_pack ("[i,I,s]", i, big, s);
    json_pack ("{s:s%}", "key", s, len);

    json_unpack (root, "{s:i}", "key", &i);
    json_unpack (root, "{s:I}", "key", &big);
    json_unpack (root, "{s:o}", "key", &o);
    json_unpack (root, "{s:s}", "key", &s);
    json_unpack (root, "{s:f}", "key", &d);

    myx_pack (h, "{s:i}", "key", i);
    myx_pack (h, "{s:I}", "key", big);
    myx_unpack (h, "{s:o}", "key", &o);
}
"""

# Each call here has exactly one intended defect, keyed by line for clarity.
BAD_C = """\
#include <stdint.h>
#include "wrap.h"
void bad (void *h, json_t *root)
{
    int i = 0;
    int64_t big = 0;
    long l = 0;
    json_int_t jbig = 0;
    json_t *o = 0;
    const char *s = 0;

    json_pack ("{s:I}", "key", i);        /* int -> I (needs 64-bit) */
    json_pack ("{s:i}", "key", big);      /* int64 -> i (needs int)  */
    json_pack ("{s:o}", "key", s);        /* char* -> o (needs json) */
    json_pack ("{s:I}", "key", l);        /* long -> I (not portable)*/
    json_pack ("[i]", l);                 /* long -> i (not portable)*/
    json_pack ("{s:s}", "key", i);        /* int -> s (needs char*)  */
    json_pack ("{s:i}", "key");           /* too few arguments       */
    json_pack ("{s:i}", "key", i, i);     /* too many arguments      */

    json_unpack (root, "{s:i}", "key", &big);  /* int64* -> i (int*) */
    json_unpack (root, "{s:o}", "key", o);     /* json_t* -> o(json**)*/
    json_unpack (root, "{s:I}", "key", &l);    /* long* -> I(64-bit*) */

    myx_pack (h, "{s:I}", "key", i);      /* wrapper: int -> I       */
    myx_unpack (h, "{s:i}", "key", &jbig);/* wrapper: json_int* ->i* */
}
"""

# Every call here must type-check cleanly per the RFC 52 D-Bus rules.
GOOD_DBUS_C = """\
#include <stdint.h>
#include "sdexec.h"
void good_dbus (flux_future_t *f, json_t *o, struct sdexec_proplist *pl,
                const char *dyntype)
{
    uint8_t y = 0;
    int b = 0, h = 0;
    int16_t n = 0;
    uint16_t q = 0;
    int32_t i = 0;
    uint32_t u = 0;
    int64_t x = 0;
    uint64_t t = 0;
    double d = 0;
    const char *s = 0, *op = 0, *g = 0;
    json_t *a = 0;
    json_t *dict = 0;

    sdexec_value_read (o, "y", &y);
    sdexec_value_read (o, "(sasb)", &s, &a, &b);
    sdexec_params_read (o, "ybnqiuxtdsogh",
                        &y, &b, &n, &q, &i, &u, &x, &t, &d, &s, &op, &g, &h);
    sdexec_params_read (o, "a{sv}", &dict);
    sdexec_params_read (o, "");
    sdexec_params_read (o, "v", "t", &t);
    sdexec_params_read (o, "sv", &s, "v", "s", &s);  /* nested variant */
    sdexec_params_read (o, "v", dyntype, &i, &s);    /* unchecked after */
    sdexec_variant_read (o, "u", &u);
    sdexec_reply_read (f, "o", &op);
    sdexec_property_get_read (f, "t", &t);
    sdexec_property_dict_read (o, "MainPID", "u", &u);

    sdexec_proplist_add (pl, "U", "u", u);
    sdexec_proplist_add (pl, "X", "x", x);
    sdexec_proplist_add (pl, "T", "t", (uint64_t)0);
    sdexec_proplist_add (pl, "D", "d", d);
    sdexec_proplist_add (pl, "S", "s", s);
    sdexec_proplist_add (pl, "H", "h", h);
}
"""

# Each call here has exactly one intended defect.  The %s is replaced with
# an over-length (>255 byte) but otherwise valid type string.
BAD_DBUS_C = """\
#include <stdint.h>
#include "sdexec.h"
void bad_dbus (flux_future_t *f, json_t *o, struct sdexec_proplist *pl)
{
    int i32 = 0;
    long l = 0;
    int64_t x64 = 0;
    uint64_t t64 = 0;
    float fl = 0;
    const char *s = 0;
    json_t *a = 0;

    sdexec_params_read (o, "t", &i32);       /* 1 int* -> t             */
    sdexec_params_read (o, "i", &x64);       /* 2 int64* -> i           */
    sdexec_params_read (o, "x", &l);         /* 3 long* -> x (platform) */
    sdexec_params_read (o, "as", a);         /* 4 json_t* -> json_t**   */
    sdexec_params_read (o, "d", &fl);        /* 5 float* -> double*     */
    sdexec_params_read (o, "(ss)", &s);      /* 6 too few arguments     */
    sdexec_params_read (o, "s", &s, &s);     /* 7 too many arguments    */
    sdexec_params_read (o, "z", &i32);       /* 8 malformed type (warn) */
    sdexec_params_read (o, "a{vs}", &a);     /* 8b non-basic dict key (warn) */
    sdexec_params_read (o, "{sv}", &a);      /* 8c bare dict entry (warn)   */
    sdexec_params_read (o, "%s");            /* 8d over-length type (warn)  */
    sdexec_params_read (o, "v", "t", &i32);  /* 9 int* -> variant t     */
    sdexec_value_read (o, "ss", &s, &s);     /* 10 two types (warn)     */
    sdexec_reply_read (f, "u", &t64);        /* 11 uint64* -> u         */

    sdexec_proplist_add (pl, "X", "x", i32); /* 12 int -> x (needs 64)  */
    sdexec_proplist_add (pl, "S", "s", i32); /* 13 int -> s             */
    sdexec_proplist_add (pl, "A", "as", a);  /* 14 not basic (warn)     */
    sdexec_proplist_add (pl, "D", "d", 1);   /* 15 int constant -> d    */
}
"""


def main():
    if not os.path.exists(CHECKER):
        sys.exit("checker not found: %s" % CHECKER)
    with tempfile.TemporaryDirectory() as d:
        for name, text in (
            ("jansson.h", JANSSON_H),
            ("wrap.h", WRAP_H),
            ("sdexec.h", SDEXEC_H),
            ("good.c", GOOD_C),
            ("bad.c", BAD_C),
            ("good_dbus.c", GOOD_DBUS_C),
            ("bad_dbus.c", BAD_DBUS_C % ("y" * 256)),
        ):
            with open(os.path.join(d, name), "w") as f:
                f.write(text)
        cc = [
            {
                "directory": d,
                "file": src,
                "arguments": ["cc", "-c", src, "-I."],
            }
            for src in ("good.c", "bad.c", "good_dbus.c", "bad_dbus.c")
        ]
        ccpath = os.path.join(d, "compile_commands.json")
        with open(ccpath, "w") as f:
            json.dump(cc, f)

        # -v so that any translation unit clang fails to analyze is reported
        # on stderr (surfaced below), turning an opaque count mismatch into a
        # diagnosable failure in CI logs.
        cmd = [sys.executable, CHECKER, "-v", "-p", ccpath]
        clang = os.environ.get("CHECK_JSON_PACK_CLANG")
        if clang:
            cmd += ["--clang", clang]
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
        )
        out = proc.stdout
        err = proc.stderr
        print(out, end="")
        print(err, file=sys.stderr, end="")

        good_lines = [
            ln
            for ln in out.splitlines()
            if ln.startswith("good.c") or ln.startswith("good_dbus.c")
        ]
        bad_lines = [ln for ln in out.splitlines() if ln.startswith("bad.c")]
        bad_dbus_lines = [ln for ln in out.splitlines() if ln.startswith("bad_dbus.c")]

        failures = []
        if good_lines:
            failures.append(
                "good fixtures produced findings (should be clean):\n  "
                + "\n  ".join(good_lines)
            )

        # bad.c has 13 intentionally defective call sites
        expected_bad = 13
        if len(bad_lines) != expected_bad:
            failures.append(
                "bad.c: expected %d findings, got %d" % (expected_bad, len(bad_lines))
            )

        # bad_dbus.c has 18 intentionally defective call sites
        expected_bad_dbus = 18
        if len(bad_dbus_lines) != expected_bad_dbus:
            failures.append(
                "bad_dbus.c: expected %d findings, got %d"
                % (expected_bad_dbus, len(bad_dbus_lines))
            )

        # spot-check a few signature messages
        must_contain = [
            "int where 64-bit json_int_t is expected",
            "64-bit value where int is expected",
            "not portable",
            "implies 2 argument(s) but 1 passed",
            "implies 2 argument(s) but 3 passed",
            "myx_pack",
            "myx_unpack",
            "expects uint64_t *",
            "expects json_t **",
            "type '(ss)' implies 2 argument(s) but 1 passed",
            "malformed type string",
            "expects a single complete type",
            "is not a basic D-Bus type",
            "255 byte limit",
            "sdexec_proplist_add() arg 1 for 'x'",
        ]
        for m in must_contain:
            if m not in out:
                failures.append("missing expected diagnostic: %r" % m)

        if failures:
            print("\nFAIL", file=sys.stderr)
            for f in failures:
                print(" - " + f, file=sys.stderr)
            sys.exit(1)
        print(
            "\nPASS: good fixtures clean, bad.c flagged %d sites, "
            "bad_dbus.c flagged %d sites" % (len(bad_lines), len(bad_dbus_lines)),
            file=sys.stderr,
        )


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# Self-test for scripts/check-json-pack.
#
# Generates stub jansson + wrapper headers and good/bad fixture sources in a
# temporary directory, builds a compile_commands.json for them, runs the
# checker, and asserts it flags exactly the intended mismatches.  Requires
# only clang (no flux build).
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


def main():
    if not os.path.exists(CHECKER):
        sys.exit("checker not found: %s" % CHECKER)
    with tempfile.TemporaryDirectory() as d:
        for name, text in (
            ("jansson.h", JANSSON_H),
            ("wrap.h", WRAP_H),
            ("good.c", GOOD_C),
            ("bad.c", BAD_C),
        ):
            with open(os.path.join(d, name), "w") as f:
                f.write(text)
        cc = [
            {
                "directory": d,
                "file": "good.c",
                "arguments": ["cc", "-c", "good.c", "-I."],
            },
            {
                "directory": d,
                "file": "bad.c",
                "arguments": ["cc", "-c", "bad.c", "-I."],
            },
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

        good_lines = [ln for ln in out.splitlines() if ln.startswith("good.c")]
        bad_lines = [ln for ln in out.splitlines() if ln.startswith("bad.c")]

        failures = []
        if good_lines:
            failures.append(
                "good.c produced findings (should be clean):\n  "
                + "\n  ".join(good_lines)
            )

        # bad.c has 13 intentionally defective call sites
        expected_bad = 13
        if len(bad_lines) != expected_bad:
            failures.append(
                "bad.c: expected %d findings, got %d" % (expected_bad, len(bad_lines))
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
            "\nPASS: good.c clean, bad.c flagged %d sites" % len(bad_lines),
            file=sys.stderr,
        )


if __name__ == "__main__":
    main()

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

#include <systemd/sd-bus.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

#include "objpath.h"

struct testvec {
    const char *xpath;
    const char *path;
};

static struct testvec opvec[] = {
    { "/object/path/foo.suffix",
      "/object/path/foo_2esuffix" },
    { "/org/freedesktop/systemd1/unit/flux-foo.service",
      "/org/freedesktop/systemd1/unit/flux_2dfoo_2eservice" },
    { "/foo/flea-bag",
      "/foo/flea_2dbag" },
    { "/foo",
      "/foo" },
    { "/",
      "/" },
};

void test_decode (sd_bus *bus)
{
    for (int i = 0; i < ARRAY_SIZE (opvec); i++) {
        char *p;
        p = objpath_encode (opvec[i].xpath);
        diag ("%s", p);
        ok (p && streq (p, opvec[i].path),
            "objpath_encode %s works", opvec[i].xpath);
        free (p);
        p = objpath_decode (opvec[i].path);
        ok (p && streq (p, opvec[i].xpath),
            "objpath_decode %s works", opvec[i].path);
        free (p);
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

    test_decode (bus);

    sd_bus_flush (bus);
    sd_bus_close (bus);
    sd_bus_unref (bus);

    done_testing ();
}

// vi: ts=4 sw=4 expandtab

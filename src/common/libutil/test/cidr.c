/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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
#include <sys/param.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/cidr.h"
#include "ccan/array_size/array_size.h"

struct cidr_test {
    const char *input;
    const char *addr;
    const char *mask;
    const char *match;
    const char *nomatch;
};

struct cidr_test testvec[] = {
    {   .input = "0.0.0.0/16",
        .addr = "0.0.0.0",
        .mask = "255.255.0.0",
        .match = "0.0.255.255",
        .nomatch = "1.1.1.1"
    },
    {   .input = "255.255.255.255/8",
        .addr = "255.255.255.255",
        .mask = "255.0.0.0",
        .match = "255.1.1.1",
        .nomatch = "254.1.1.1"
    },
    {   .input = "192.168.0.0/24",
        .addr = "192.168.0.0",
        .mask = "255.255.255.0",
        .match = "192.168.0.1",
        .nomatch = "192.168.1.1"
    },
    {   .input = "192.168.0.1/32", // "host route" in RFC 4632
        .addr = "192.168.0.1",
        .mask = "255.255.255.255",
        .match = "192.168.0.1",
        .nomatch = "192.168.0.2"
    },
    {   .input = "192.168.0.0",
        .addr = "192.168.0.0",
        .mask = "255.255.255.255",
        .match = "192.168.0.0",
        .nomatch = "192.168.0.1"
    },
    {   .input = "0.0.0.0/0",   // "default route" in RFC 4632
        .addr = "0.0.0.0",
        .mask = "0.0.0.0",
        .match = "255.255.255.255",
        NULL    // skip
    },
};

bool addr_is (struct in_addr *addr, const char *s)
{
    struct in_addr a;
    if (inet_pton (AF_INET, s, &a) < 0 || a.s_addr != addr->s_addr)
        return false;
    return true;
}

bool match_addr (struct cidr4 *cidr, const char *s)
{
    struct in_addr a;
    if (inet_pton (AF_INET, s, &a) < 0 || !cidr_match4 (cidr, &a))
        return false;
    return true;
}

int main (int argc, char** argv)
{
    int n;
    struct cidr4 cidr;
    struct in_addr addr;

    plan (NO_PLAN);

    for (int i = 0; i < ARRAY_SIZE (testvec); i++) {
        n = cidr_parse4 (&cidr, testvec[i].input);
        ok (n == 0
            && addr_is (&cidr.addr, testvec[i].addr)
            && addr_is (&cidr.mask, testvec[i].mask),
            "%s => %s/%s",
            testvec[i].input,
            testvec[i].addr,
            testvec[i].mask);

        if (testvec[i].match) {
            ok (match_addr (&cidr, testvec[i].match) == true,
                "%s matches", testvec[i].match);
        }
        if (testvec[i].nomatch) {
            ok (match_addr (&cidr, testvec[i].nomatch) == false,
                "%.8x does not match", testvec[i].nomatch);
        }
    }

    errno = 0;
    ok (cidr_parse4 (NULL, "0.0.0.0") < 0 && errno == EINVAL,
        "cidr_parse4 cidr=NULL fails with EINVAL");
    errno = 0;
    ok (cidr_parse4 (&cidr, NULL) < 0 && errno == EINVAL,
        "cidr_parse4 s=NULL fails with EINVAL");

    ok (cidr_match4 (NULL, &addr) == false,
        "cidr_match4 cidr=NULL returns false");
    ok (cidr_match4 (&cidr, NULL) == false,
        "cidr_match4 addr=NULL returns false");

    done_testing ();
}

// vi:ts=4 sw=4 expandtab

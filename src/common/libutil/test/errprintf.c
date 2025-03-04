/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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

#include <string.h>
#include <errno.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/errprintf.h"

int main(int argc, char** argv)
{
    flux_error_t error;
    char longstring[256];

    lives_ok ({err_init (NULL);},
              "err_init with NULL args doesn't crash");

    lives_ok ({errprintf (NULL, NULL);},
              "errprintf with no args doesn't crash");
    lives_ok ({errprintf (&error, NULL);},
              "errprintf with NULL format doesn't crash");
    is (error.text, "",
        "and returned error is empty");

    errprintf (&error, "foo");
    is (error.text, "foo",
        "errprintf with static format works");

    err_init (&error);
    is (error.text, "",
        "err_init zeros error.text buffer");

    errno = 64;
    errprintf (&error, "foo");
    ok (errno == 64,
        "errprintf preserves error");

    errprintf (&error, "%s: %s", "foo", "bar");
    is (error.text, "foo: bar",
        "errprintf with simple format works");

    for (int i = 0; i < sizeof (longstring) - 1; i++)
        longstring[i] = 'x';
    longstring[sizeof (longstring) - 1] = '\0';

    errprintf (&error, "%s", longstring);
    ok (strlen (error.text) == sizeof (error.text) - 1,
        "errprintf with too long format properly truncates");
    ok (error.text[sizeof (error.text) - 2] == '+',
        "errprintf notes truncation with a '+'");
    longstring[sizeof (error.text) - 1] = '\0';
    longstring[sizeof (error.text) - 2] = '+';
    is (error.text, longstring,
        "error is expected");

    ok (errprintf (&error, "Test error") == -1,
        "errprintf() always returns -1");

    done_testing();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

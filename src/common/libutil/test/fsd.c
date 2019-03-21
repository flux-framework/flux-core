/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <string.h>
#include <errno.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/fsd.h"

int main(int argc, char** argv)
{
    double d;
    char buf [64];
    plan (NO_PLAN);

    ok (fsd_parse_duration (NULL, NULL) < 0 && errno == EINVAL,
        "fsd_parse_duration (NULL, NULL) is an error");
    ok (fsd_parse_duration (NULL, &d) < 0 && errno == EINVAL,
        "fsd_parse_duration (NULL, &d) is an error");
    ok (fsd_parse_duration ("0", NULL) < 0 && errno == EINVAL,
        "fsd_parse_duration (s, NULL) is an error");
    ok (fsd_parse_duration ("-1.", &d) < 0 && errno == EINVAL,
        "fsd_parse_duration (-1) is an error");
    ok (fsd_parse_duration ("1.0f", &d) < 0 && errno == EINVAL,
        "fsd_parse_duration (1.0f) is an error");
    ok (fsd_parse_duration ("1.0sec", &d) < 0 && errno == EINVAL,
        "fsd_parse_duration (1.0sec) is an error");

    ok (fsd_parse_duration ("0", &d) == 0,
        "fsd_parse_duration (0) returns success");
    ok (d == 0., "got d == %g", d);

    ok (fsd_parse_duration ("0s", &d) == 0,
        "fsd_parse_duration (0s) returns success");
    ok (d == 0., "got d == %g", d);

    ok (fsd_parse_duration ("0m", &d) == 0,
        "fsd_parse_duration (0m) returns success");
    ok (d == 0., "got d == %g", d);

    ok (fsd_parse_duration ("0h", &d) == 0,
        "fsd_parse_duration (0h) returns success");
    ok (d == 0., "got d == %g", d);

    ok (fsd_parse_duration ("0d", &d) == 0,
        "fsd_parse_duration (0d) returns success");
    ok (d == 0., "got d == %g", d);

    ok (fsd_parse_duration ("0.5", &d) == 0,
        "fsd_parse_duration (0.5) returns success");
    ok (d == 0.5, "got d == %g", d);

    ok (fsd_parse_duration ("0.5s", &d) == 0,
        "fsd_parse_duration (0.5s) returns success");
    ok (d == 0.5, "got d == %g", d);

    ok (fsd_parse_duration ("0.5m", &d) == 0,
        "fsd_parse_duration (0.5m) returns success");
    ok (d == 30., "got d == %g", d);
    
    ok (fsd_parse_duration ("0.5h", &d) == 0,
        "fsd_parse_duration (0.5h) returns success");
    ok (d == .5 * 60. * 60., "got d == %g", d);

    ok (fsd_parse_duration ("1.0d", &d) == 0,
        "fsd_parse_duration (1.0d) returns success");
    ok (d == 24. * 60. * 60., "got d == %g", d);

    ok (fsd_format_duration (buf, sizeof (buf), -1.) < 0 && errno == EINVAL,
        "fsd_format_duration with duration < 0. returns EINVAL");

    ok (fsd_format_duration (buf, 0, 1.) < 0 && errno == EINVAL,
        "fsd_format_duration with buflen <= 0. returns EINVAL");

    ok (fsd_format_duration (NULL, 1024, 1000.) < 0 && errno == EINVAL,
        "fsd_format_duration with buf == NULL returns EINVAL");
    
    ok (fsd_format_duration (buf, sizeof (buf), .001),
        "fsd_format_duration (.001) works");
    is (buf, "0.001s", "returns expected string = %s", buf);

    ok (fsd_format_duration (buf, sizeof (buf), 5.),
        "fsd_format_duration (5.0) works");
    is (buf, "5s", "returns expected string = %s", buf);

    ok (fsd_format_duration (buf, sizeof (buf), 5.1),
        "fsd_format_duration (5.1) works");
    is (buf, "5.1s", "returns expected string = %s", buf);

    ok (fsd_format_duration (buf, sizeof (buf), 62.0),
        "fsd_format_duration (62.) works");
    is (buf, "1.03333m", "returns expected string = %s", buf);

    ok (fsd_format_duration (buf, sizeof (buf), 3600.),
        "fsd_format_duration (3600.) works");
    is (buf, "1h", "returns expected string = %s", buf);

    ok (fsd_format_duration (buf, sizeof (buf), 4500.),
        "fsd_format_duration (4500.) works");
    is (buf, "1.25h", "returns expected string = %s", buf);

    ok (fsd_format_duration (buf, sizeof (buf), 86400.),
        "fsd_format_duration (86400.) works");
    is (buf, "1d", "returns expected string = %s", buf);

    ok (fsd_format_duration (buf, sizeof (buf), 103680.0),
        "fsd_format_duration (86400.) works");
    is (buf, "1.2d", "returns expected string = %s", buf);

    done_testing();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

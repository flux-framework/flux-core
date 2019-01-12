/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <iostream>
#include <string>
#include <jansson.h>
#include <assert.h>

#include "src/common/libjobspec/jobspec.hpp"

using namespace std;
using namespace Flux::Jobspec;

void respond (int errnum, const char *errstr)
{
    json_t *o;
    char *s;

    if (errstr)
        o = json_pack ("{s:i s:s}", "errnum", errnum, "errstr", errstr);
    else
        o = json_pack ("{s:i}", "errnum", errnum);
    assert (o != NULL);
    s = json_dumps (o, JSON_COMPACT);
    assert (s != NULL);
    cout << s;
    cout << "\n";
    json_decref (o);
    free (s);
}

int main(int argc, char *argv[])
{
    std:string line;

    while (std::getline (cin, line)) {
        Jobspec js;
        try {
            js = Jobspec (line);
            respond (0, NULL);
        } catch (parse_error& e) {
            respond (1, e.what ());
        }
    }

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

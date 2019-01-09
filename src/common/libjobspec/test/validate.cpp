/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <iostream>
#include <fstream>
#include <string>

#include "jobspec.hpp"
#include <yaml-cpp/yaml.h>

using namespace std;
using namespace Flux::Jobspec;

void parse_yaml_stream_docs (std::istream& js_stream)
{
    bool first = true;
    for (auto&& rootnode: YAML::LoadAll (js_stream)) {
        Jobspec js;
        if (!first)
            cout << endl;
        else
            first = false;
        js = Jobspec (rootnode);
        cout << js;
    }
}

int main(int argc, char *argv[])
{
    try {
        if (argc == 1) {
            parse_yaml_stream_docs (cin);
        } else {
            for (int i = 1; i < argc; i++) {
                std::ifstream js_file (argv[i]);
                if (js_file.fail()) {
                    cerr << argv[0] << ": Unable to open file \"" << argv[i] << "\"" << endl;
                    return 1;
                }
                parse_yaml_stream_docs (js_file);
            }
        }
    } catch (parse_error& e) {
        cerr << argv[0] << ": ";
        if (e.position != -1)
            cerr << argv[0] << "position " << e.position << ", ";
        if (e.line != -1)
            cerr << argv[0] << "line " << e.line << ", ";
        if (e.column != -1)
            cerr << "column " << e.column << ", ";
        cerr << e.what() << endl;
        return 2;
    } catch (...) {
        cerr << argv[0] << ": Unknown non-standard exception" << endl;
        return 3;
    }

    return 0;
}

/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#include <iostream>
#include <fstream>
#include <string>

#include <flux/jobspec.hpp>
#include <yaml-cpp/yaml.h>

using namespace std;
using namespace Flux::Jobspec;

void validate_jobspec (Jobspec js)
{
    cout << "version = " << js.version << endl;
    for (auto&& resource : js.resources) {
        cout << "type = " << resource.type << endl;
        cout << "count min = " << resource.count.min << endl;
        cout << "count max = " << resource.count.max << endl;
        cout << "count operator = " << resource.count.oper << endl;
        cout << "count operand = " << resource.count.operand << endl;
    }
    for (auto&& task : js.tasks) {
        cout << "command =";
        for (auto&& field : task.command) {
            cout << " " << field;
        }
        cout << endl;
    }
}

void parse_yaml_stream_docs (std::istream& js_stream)
{
    bool first = true;
    for (auto&& rootnode: YAML::LoadAll (js_stream)) {
        if (!first)
            cout << endl;
        else
            first = false;
        validate_jobspec (Jobspec (rootnode));
        cout << "--------------------------------------------" << endl;
    }
}

int main(int argc, char *argv[])
{
    if (argc == 1) {
        parse_yaml_stream_docs (cin);
    } else {
        for (int i = 1; i < argc; i++) {
            std::ifstream js_file (argv[i]);
            if (js_file.fail()) {
                cerr << "Unable to open file \"" << argv[i] << "\"" << endl;
                continue;
            }
            parse_yaml_stream_docs (js_file);
        }
    }
    return 0;
}

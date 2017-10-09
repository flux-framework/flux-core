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

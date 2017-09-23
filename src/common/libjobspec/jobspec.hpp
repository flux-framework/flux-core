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

#ifndef jobspec_hpp
#define jobspec_hpp

#include <iostream>
#include <unordered_map>
#include <cstdint>
#include <yaml-cpp/yaml.h>

namespace Flux {
namespace Jobspec {

enum class tristate_t { FALSE, TRUE, UNSPECIFIED };

class Resource {
public:
    std::string type;
    struct {
        unsigned min;
        unsigned max;
        char oper = '+';
        int operand = 1;
    } count;
    std::string unit;
    std::string label;
    std::string id;
    tristate_t exclusive = tristate_t::UNSPECIFIED;
    std::vector<Resource> with;

    // user_data has no library internal usage, it is
    // entirely for the convenience of external code
    std::unordered_map<std::string, int64_t> user_data;

    Resource(const YAML::Node&);

private:
    void parse_yaml_count(const YAML::Node&);
};

class Task {
public:
    std::vector<std::string> command;
    std::string slot;
    std::unordered_map<std::string, std::string> count;
    std::string distribution;
    std::unordered_map<std::string, std::string> attributes;

    Task(const YAML::Node&);
};

class Jobspec {
public:
    unsigned int version;
    std::vector<Resource> resources;
    std::vector<Task> tasks;
    std::unordered_map<std::string,
        std::unordered_map<std::string, std::string>> attributes;
    Jobspec() = default;
    Jobspec(const YAML::Node&);
    Jobspec(std::istream &is): Jobspec{YAML::Load(is)} {}
    Jobspec(std::string &s): Jobspec{YAML::Load(s)} {}
};

} // namespace Jobspec
} // namespace Flux

#endif // jobspec_hpp

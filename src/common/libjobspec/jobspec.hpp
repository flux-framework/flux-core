/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/*
 * This jobspec module handles parsing the Flux jobspec format as specified
 * in Spec 14 in the Flux RFC project: https://github.com/flux-framework/rfc
 *
 * The primary interface in the library is the Flux:Jobspec::Jobspec class.
 * The constructor Flux::Jobspec::Jobspec() can handle jobspec data in a
 * std::string, std::istream, or the top document YAML::Node as pre-processed
 * by the yaml-cpp library.
 *
 * When errors are found in the jobspec stream the library will raise the
 * Flux::Jobspec:parse_error exception.  If the library was able to determine
 * the location that the error occurred in jobspec yaml stream, it will appear
 * in the position, line, and column members of the parse_error object.  If it
 * is unable to determine the location, all three of those fields will be -1.
 *
 * NOTE: The library will only be able to determine the location of error with
 * yaml-cpp version 0.5.3 or newer.
 */

#ifndef JOBSPEC_HPP
#define JOBSPEC_HPP

#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <cstdint>
#include <yaml-cpp/yaml.h>

namespace Flux {
namespace Jobspec {

class parse_error : public std::runtime_error {
public:
    int position;
    int line;
    int column;
    parse_error(const char *msg);
    parse_error(const YAML::Node& node, const char *msg);
};

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
    Jobspec(std::istream &is);
    Jobspec(std::string &s);
};

std::ostream& operator<<(std::ostream& s, Jobspec const& js);
std::ostream& operator<<(std::ostream& s, Resource const& r);
std::ostream& operator<<(std::ostream& s, Task const& t);

} // namespace Jobspec
} // namespace Flux

#endif // JOBSPEC_HPP

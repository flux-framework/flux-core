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

#include "jobspec.hpp"

#include <iostream>
#include <string>
#include <unordered_map>

extern "C" {
#if HAVE_CONFIG_H
#include "config.h"
#endif
}

using namespace std;
using namespace Flux::Jobspec;

parse_error::parse_error(const char *msg)
    : runtime_error(msg),
      position(-1),
      line(-1),
      column(-1)
{}

parse_error::parse_error(const YAML::Node &node, const char *msg)
    : runtime_error(msg),
#ifdef HAVE_YAML_MARK
      position(node.Mark().pos),
      line(node.Mark().line+1),
      column(node.Mark().column)
#else
      position(-1),
      line(-1),
      column(-1)
#endif
{}

namespace {
void parse_yaml_count (Resource& res, const YAML::Node &cnode)
{
    /* count can have an unsigned interger value */
    if (cnode.IsScalar()) {
        res.count.min = cnode.as<unsigned>();
        res.count.max = res.count.min;
        return;
    }

    /* or count may be a more complicated verbose form */
    if (!cnode.IsMap()) {
        throw parse_error (cnode, "count is not a mapping");
    }

    /* Verify existance of required entries */
    if (!cnode["min"]) {
        throw parse_error (cnode, "Key \"min\" missing from count");
    }
    if (!cnode["min"].IsScalar()) {
        throw parse_error (cnode["min"], "Value of \"min\" must be a scalar");
    }
    if (!cnode["max"]) {
        throw parse_error (cnode, "Key \"max\" missing from count");
    }
    if (!cnode["max"].IsScalar()) {
        throw parse_error (cnode["max"], "Value of \"max\" must be a scalar");
    }
    if (!cnode["operator"]) {
        throw parse_error (cnode, "Key \"operator\" missing from count");
    }
    if (!cnode["operator"].IsScalar()) {
        throw parse_error (cnode["operator"],
                           "Value of \"operator\" must be a scalar");
    }
    if (!cnode["operand"]) {
        throw parse_error (cnode, "Key \"operand\" missing from count");
    }
    if (!cnode["operand"].IsScalar()) {
        throw parse_error (cnode["operand"],
                           "Value of \"operand\" must be a scalar");
    }

    /* Validate values of entries */
    res.count.min = cnode["min"].as<unsigned>();
    if (res.count.min < 1) {
        throw parse_error (cnode["min"], "\"min\" must be greater than zero");
    }

    res.count.max = cnode["max"].as<unsigned>();
    if (res.count.max < 1) {
        throw parse_error (cnode["max"], "\"max\" must be greater than zero");
    }
    if (res.count.max < res.count.min) {
        throw parse_error (cnode["max"],
                           "\"max\" must be greater than or equal to \"min\"");
    }

    res.count.oper = cnode["operator"].as<char>();
    switch (res.count.oper) {
    case '+':
    case '*':
    case '^':
        break;
    default:
        throw parse_error (cnode["operator"], "Invalid count operator");
    }

    res.count.operand = cnode["operand"].as<int>();
}
}

namespace {
vector<Resource> parse_yaml_resources (const YAML::Node &resources);
}

Resource::Resource (const YAML::Node &resnode)
{
    unsigned field_count = 0;

    /* The resource must be a mapping */
    if (!resnode.IsMap()) {
        throw parse_error (resnode, "resource is not a mapping");
    }
    if (!resnode["type"]) {
        throw parse_error (resnode, "Key \"type\" missing from resource");
    }
    if (!resnode["type"].IsScalar()) {
        throw parse_error (resnode["type"],
                           "Value of \"type\" must be a scalar");
    }
    type = resnode["type"].as<string>();
    field_count++;

    if (!resnode["count"]) {
        throw parse_error (resnode, "Key \"count\" missing from resource");
    }
    parse_yaml_count (*this, resnode["count"]);
    field_count++;

    if (resnode["unit"]) {
        if (!resnode["unit"].IsScalar()) {
            throw parse_error (resnode["unit"],
                               "Value of \"unit\" must be a scalar");
        }
        field_count++;
        unit = resnode["unit"].as<string>();
    }
    if (resnode["exclusive"]) {
        if (!resnode["exclusive"].IsScalar()) {
            throw parse_error (resnode["exclusive"],
                               "Value of \"exclusive\" must be a scalar");
        }
        field_count++;
        string val = resnode["exclusive"].as<string>();
        if (val == "false") {
            exclusive = tristate_t::FALSE;
        } else if (val == "true") {
            exclusive = tristate_t::TRUE;
        } else {
            throw parse_error (resnode["exclusive"],
                               "Value of \"exclusive\" must be either \"true\" or \"false\"");
        }
    }

    if (resnode["with"]) {
        field_count++;
        with = parse_yaml_resources (resnode["with"]);
    }

    if (resnode["label"]) {
        if (!resnode["label"].IsScalar()) {
            throw parse_error (resnode["label"],
                               "Value of \"label\" must be a scalar");
        }
        field_count++;
        label = resnode["label"].as<string>();
    } else if (type == "slot") {
        throw parse_error (resnode, "All slots must be labeled");
    }

    if (resnode["id"]) {
        if (!resnode["id"].IsScalar()) {
            throw parse_error (resnode["id"],
                               "Value of \"id\" must be a scalar");
        }
        field_count++;
        id = resnode["id"].as<string>();
    }

    if (field_count != resnode.size()) {
        throw parse_error (resnode, "Unrecognized key in resource mapping");
    }

    if (resnode.size() < 2 || resnode.size() > 10) {
        throw parse_error (resnode,
                           "impossible number of entries in resource mapping");
    }

}

Task::Task (const YAML::Node &tasknode)
{
    /* The task node must be a mapping */
    if (!tasknode.IsMap()) {
        throw parse_error (tasknode, "task is not a mapping");
    }
    if (!tasknode["command"]) {
        throw parse_error (tasknode, "Key \"command\" missing from task");
    }
    if (tasknode["command"].IsSequence()) {
        command = tasknode["command"].as<vector<string>>();
    } else if (tasknode["command"].IsScalar()) {
        command.push_back(tasknode["command"].as<string>());
    } else {
        throw parse_error (tasknode["command"],
                           "\"command\" value must be a scalar or a sequence");
    }

    /* Import slot */
    if (!tasknode["slot"]) {
        throw parse_error (tasknode, "Key \"slot\" missing from task");
    }
    if (!tasknode["slot"].IsScalar()) {
        throw parse_error (tasknode["slot"],
                           "Value of task \"slot\" must be a YAML scalar");
    }
    slot = tasknode["slot"].as<string>();

    /* Import count mapping */
    if (tasknode["count"]) {
        YAML::Node count_node = tasknode["count"];
        if (!count_node.IsMap()) {
            throw parse_error (count_node,
                               "\"count\" in task is not a mapping");
        }
        for (auto&& entry : count_node) {
            count[entry.first.as<string>()] = entry.second.as<string>();
        }
    }

    /* Import distribution if it is present */
    if (tasknode["distribution"]) {
        if (!tasknode["distribution"].IsScalar()) {
            throw parse_error (tasknode["distribution"],
                               "Value of task \"distribution\" must be a YAML scalar");
        }
        distribution = tasknode["distribution"].as<string>();
    }

    /* Import attributes mapping if it is present */
    if (tasknode["attributes"]) {
        YAML::Node attrs = tasknode["attributes"];
        if (!attrs.IsMap()) {
            throw parse_error (attrs, "\"attributes\" in task is not a mapping");
        }
        for (auto&& attr : attrs) {
            attributes[attr.first.as<string>()] = attr.second.as<string>();
        }
    }

    if (tasknode.size() < 3 || tasknode.size() > 5) {
        throw parse_error (tasknode,
                           "impossible number of entries in task mapping");
    }
}

namespace {
vector<Task> parse_yaml_tasks (const YAML::Node &tasks)
{
    vector<Task> taskvec;

    /* "tasks" must be a sequence */
    if (!tasks.IsSequence()) {
        throw parse_error (tasks, "\"tasks\" is not a sequence");
    }

    for (auto&& task : tasks) {
        taskvec.push_back (Task (task));
    }

    return taskvec;
}
}

namespace {
vector<Resource> parse_yaml_resources (const YAML::Node &resources)
{
    vector<Resource> resvec;

    /* "resources" must be a sequence */
    if (!resources.IsSequence()) {
        throw parse_error (resources, "\"resources\" is not a sequence");
    }

    for (auto&& resource : resources) {
        resvec.push_back (Resource (resource));
    }

    return resvec;
}
}

Jobspec::Jobspec(const YAML::Node &top)
{
    try {
        /* The top yaml node of the jobspec must be a mapping */
        if (!top.IsMap()) {
            throw parse_error (top, "Top level of jobspec is not a mapping");
        }
        /* The four keys must be the following */
        if (!top["version"]) {
            throw parse_error (top, "Missing key \"version\" in top level mapping");
        }
        if (!top["resources"]) {
            throw parse_error (top, "Missing key \"resource\" in top level mapping");
        }
        if (!top["tasks"]) {
            throw parse_error (top, "Missing key \"tasks\" in top level mapping");
        }
        if (!top["attributes"]) {
            throw parse_error (top, "Missing key \"attributes\" in top level mapping");
        }
        /* There must be exactly four entries in the mapping */
        if (top.size() != 4) {
            throw parse_error (top, "Top mapping in jobspec must have exactly four entries");
        }

        /* Import version */
        if (!top["version"].IsScalar()) {
            throw parse_error (top["version"],
                               "\"version\" must be an unsigned integer");
        }
        version = top["version"].as<unsigned int>();
        if (version != 1) {
            throw parse_error (top["version"],
                               "Only jobspec \"version\" 1 is supported");
        }

        /* Import attributes mappings */
        YAML::Node attrs = top["attributes"];
        /* allow attributes to be present and empty */
        if (!attrs.IsNull()) {
            if (!attrs.IsMap()) {
                throw parse_error (attrs, "\"attributes\" is not a mapping");
            }
            if (attrs.size() > 0) {
                for (auto&& i : attrs) {
                    if (!i.second.IsMap()) {
                        throw parse_error (i.second,
                                           "value of attribute is not a mapping");
                    }
                    for (auto&& j : i.second) {
                        attributes[i.first.as<string>()][j.first.as<string>()] =
                            j.second.as<string>();
                    }
                }
            }
        }
        /* Import resources section */
        resources = parse_yaml_resources (top["resources"]);

        /* Import tasks section */
        tasks = parse_yaml_tasks (top["tasks"]);
    } catch (YAML::Exception& e) {
        throw parse_error(e.what());
    }
}

Jobspec::Jobspec(std::istream &is)
try
    : Jobspec {YAML::Load (is)}
{
}
catch (YAML::Exception& e) {
    throw parse_error(e.what());
}

Jobspec::Jobspec(std::string &s)
try
    : Jobspec {YAML::Load (s)}
{
}
catch (YAML::Exception& e) {
    throw parse_error(e.what());
}

namespace {
/*
 * This class magically makes everything in 'dest' stream
 * indented as long as this class is in scope.  Once it
 * it goes out of scope and is destroyed, the indenting
 * disappears.
 */
class IndentingOStreambuf : public std::streambuf
{
    std::streambuf* myDest;
    bool myIsAtStartOfLine;
    std::string myIndent;
    std::ostream* myOwner;
protected:
    virtual int overflow( int ch )
    {
        if (myIsAtStartOfLine && ch != '\n') {
            myDest->sputn (myIndent.data(), myIndent.size());
        }
        myIsAtStartOfLine = ch == '\n';
        return myDest->sputc (ch);
    }
public:
    explicit IndentingOStreambuf (std::streambuf* dest, int indent = 2)
        : myDest (dest)
        , myIsAtStartOfLine (true)
        , myIndent (indent, ' ')
        , myOwner(NULL)
    {
    }
    explicit IndentingOStreambuf (std::ostream& dest, int indent = 2)
        : myDest (dest.rdbuf ())
        , myIsAtStartOfLine (true)
        , myIndent (indent, ' ')
        , myOwner (&dest)
    {
        myOwner->rdbuf (this);
    }
    virtual ~IndentingOStreambuf()
    {
        if ( myOwner != NULL ) {
            myOwner->rdbuf (myDest);
        }
    }
};
}

std::ostream& Flux::Jobspec::operator<<(std::ostream& s, Jobspec const& jobspec)
{
    s << "version: " << jobspec.version << endl;
    s << "resources: " << endl;
    for (auto&& resource : jobspec.resources) {
        IndentingOStreambuf indent (s);
        s << resource;
    }
    s << "tasks: " << endl;
    for (auto&& task : jobspec.tasks) {
        IndentingOStreambuf indent (s);
        s << task;
    }
    s << "attributes:" << endl;
    for (auto&& subattr : jobspec.attributes) {
        s << "  " << subattr.first << ":" << endl;
        for (auto&& attr : subattr.second) {
            s << "    " << attr.first << " = " << attr.second << endl;
        }
    }

    return s;
}

std::ostream& Flux::Jobspec::operator<<(std::ostream& s,
                                        Resource const& resource)
{
    s << "- type: " << resource.type << endl;
    s << "  count:" << endl;
    s << "    min:  " << resource.count.min << endl;
    s << "    max: " << resource.count.max << endl;
    s << "    operator: " << resource.count.oper << endl;
    s << "    operand: " << resource.count.operand << endl;
    if (resource.unit.size() > 0)
        s << "  unit: " << resource.unit << endl;
    if (resource.label.size() > 0)
        s << "  label: " << resource.label << endl;
    if (resource.id.size() > 0)
        s << "  id: " << resource.id << endl;
    if (resource.exclusive == tristate_t::TRUE)
            s << "  exclusive: true" << endl;
    else if (resource.exclusive == tristate_t::FALSE)
            s << "  exclusive: false" << endl;
    if (resource.with.size() > 0) {
        s << "  with:" << endl;
        IndentingOStreambuf indent (s, 4);
        for (auto&& child_resource : resource.with) {
            s << child_resource;
        }
    }

    return s;
}

std::ostream& Flux::Jobspec::operator<<(std::ostream& s,
                                        Task const& task)
{
    bool first = true;
    s << "command: [ ";
    for (auto&& field : task.command) {
        if (!first)
            s << ", ";
        else
            first = false;
        s << "\"" << field << "\"";
    }
    s << " ]" << endl;
    s << "slot: " << task.slot << endl;
    if (task.count.size() > 0) {
        s << "count:" << endl;
        IndentingOStreambuf indent (s);
        for (auto&& c : task.count) {
            s << c.first << ": " << c.second << endl;
        }
    }
    if (task.distribution.size() > 0)
        s << "distribution: " << task.distribution << endl;
    if (task.attributes.size() > 0) {
        s << "attributes:" << endl;
        IndentingOStreambuf indent (s);
        for (auto&& attr : task.attributes) {
            s << attr.first << ": " << attr.second;
        }
    }

    return s;
}

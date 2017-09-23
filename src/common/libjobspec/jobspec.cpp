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

#include "jobspec.hpp"

#include <iostream>
#include <string>
#include <unordered_map>

using namespace std;
using namespace Flux::Jobspec;

void Resource::parse_yaml_count (const YAML::Node &cnode)
{
    /* count can have an unsigned interger value */
    if (cnode.IsScalar()) {
        count.min = cnode.as<unsigned>();
        count.max = count.min;
        return;
    }

    /* or count may be a more complicated verbose form */
    if (!cnode.IsMap()) {
        throw invalid_argument ("count is not a mapping");
    }

    /* Verify existance of required entries */
    if (!cnode["min"]) {
        throw invalid_argument ("Key \"min\" missing from count");
    }
    if (!cnode["min"].IsScalar()) {
        throw invalid_argument ("Value of \"min\" must be a scalar");
    }
    if (!cnode["max"]) {
        throw invalid_argument ("Key \"max\" missing from count");
    }
    if (!cnode["max"].IsScalar()) {
        throw invalid_argument ("Value of \"max\" must be a scalar");
    }
    if (!cnode["operator"]) {
        throw invalid_argument("Key \"operator\" missing from count");
    }
    if (!cnode["operator"].IsScalar()) {
        throw invalid_argument ("Value of \"operator\" must be a scalar");
    }
    if (!cnode["operand"]) {
        throw invalid_argument ("Key \"operand\" missing from count");
    }
    if (!cnode["operand"].IsScalar()) {
        throw invalid_argument ("Value of \"operand\" must be a scalar");
    }

    /* Validate values of entries */
    count.min = cnode["min"].as<unsigned>();
    if (count.min < 1) {
        throw invalid_argument ("\"min\" must be greater than zero");
    }

    count.max = cnode["max"].as<unsigned>();
    if (count.max < 1) {
        throw invalid_argument ("\"max\" must be greater than zero");
    }
    if (count.max < count.min) {
        throw invalid_argument ("\"max\" must be greater than or equal to \"min\"");
    }

    count.oper = cnode["operator"].as<char>();
    switch (count.oper) {
    case '+':
    case '-':
    case '^':
        break;
    default:
        throw invalid_argument("Invalid count operator");
    }

    count.operand = cnode["operand"].as<int>();
}

namespace {
vector<Resource> parse_yaml_resources (const YAML::Node &resources);
}

Resource::Resource (const YAML::Node &resnode)
{
    int field_count = 0;

    /* The resource must be a mapping */
    if (!resnode.IsMap()) {
        throw invalid_argument ("resource is not a mapping");
    }
    if (resnode.size() < 2 || resnode.size() > 10) {
        throw invalid_argument ("impossible number of entries in resource mapping");
    }

    if (!resnode["type"]) {
        throw invalid_argument ("Key \"type\" missing from resource");
    }
    if (!resnode["type"].IsScalar()) {
        throw invalid_argument ("Value of \"type\" must be a scalar");
    }
    type = resnode["type"].as<string>();
    field_count++;

    if (!resnode["count"]) {
        throw invalid_argument ("Key \"count\" missing from resource");
    }
    parse_yaml_count (resnode["count"]);
    field_count++;

    if (resnode["unit"]) {
        if (!resnode["unit"].IsScalar()) {
            throw invalid_argument ("Value of \"unit\" must be a scalar");
        }
        field_count++;
        unit = resnode["unit"].as<string>();
    }
    if (resnode["exclusive"]) {
        if (!resnode["exclusive"].IsScalar()) {
            throw invalid_argument ("Value of \"exclusive\" must be a scalar");
        }
        field_count++;
        string val = resnode["exclusive"].as<string>();
        if (val == "false") {
            exclusive = tristate_t::FALSE;
        } else if (val == "true") {
            exclusive = tristate_t::TRUE;
        } else {
            throw invalid_argument ("Value of \"exclusive\" must be either \"true\" or \"false\"");
        }
    }

    if (resnode["with"]) {
        field_count++;
        with = parse_yaml_resources (resnode["with"]);
    }

    if (resnode["label"]) {
        if (!resnode["label"].IsScalar()) {
            throw invalid_argument ("Value of \"label\" must be a scalar");
        }
        field_count++;
        unit = resnode["label"].as<string>();
    } else if (type == "slot") {
        throw invalid_argument ("All slots must be labeled");
    }

    if (resnode["id"]) {
        if (!resnode["id"].IsScalar()) {
            throw invalid_argument ("Value of \"id\" must be a scalar");
        }
        field_count++;
        id = resnode["id"].as<string>();
    }

    if (field_count != resnode.size()) {
        throw invalid_argument ("Unrecognized key in resource mapping");
    }
}

Task::Task (const YAML::Node &tasknode)
{
    /* The task node must be a mapping */
    if (!tasknode.IsMap()) {
        throw invalid_argument ("task is not a mapping");
    }
    if (tasknode.size() < 3 || tasknode.size() > 5) {
        throw invalid_argument ("impossible number of entries in task mapping");
    }
    if (!tasknode["command"]) {
        throw invalid_argument ("Key \"command\" missing from task");
    }
    if (tasknode["command"].IsSequence()) {
        command = tasknode["command"].as<vector<string>>();
    } else if (tasknode["command"].IsScalar()) {
        command.push_back(tasknode["command"].as<string>());
    } else {
        throw invalid_argument ("\"command\" value must be a scalar or a sequence");
    }

    /* Import slot */
    if (!tasknode["slot"]) {
        throw invalid_argument ("Key \"slot\" missing from task");
    }
    if (!tasknode["slot"].IsScalar()) {
        throw invalid_argument ("Value of task \"slot\" must be a YAML scalar");
    }
    slot = tasknode["slot"].as<string>();

    /* Import count mapping */
    if (tasknode["count"]) {
        YAML::Node count = tasknode["count"];
        if (!count.IsMap()) {
            throw invalid_argument ("\"count\" in task is not a mapping");
        }
        for (auto&& entry : count) {
            count[entry.first.as<string>()] = entry.second.as<string>();
        }
    }

    /* Import distribution if it is present */
    if (tasknode["distribution"]) {
        if (!tasknode["distribution"].IsScalar()) {
            throw invalid_argument ("Value of task \"distribution\" must be a YAML scalar");
        }
        distribution = tasknode["distribution"].as<string>();
    }

    /* Import attributes mapping if it is present */
    if (tasknode["attributes"]) {
        YAML::Node attrs = tasknode["attributes"];
        if (!attrs.IsMap()) {
            throw invalid_argument ("\"attributes\" in task is not a mapping");
        }
        for (auto&& attr : attrs) {
            attributes[attr.first.as<string>()] = attr.second.as<string>();
        }
    }
}

vector<Task> parse_yaml_tasks (const YAML::Node &tasks)
{
    vector<Task> taskvec;

    /* "tasks" must be a sequence */
    if (!tasks.IsSequence()) {
        throw invalid_argument ("\"tasks\" is not a sequence");
    }

    for (auto&& task : tasks) {
        taskvec.push_back (Task (task));
    }

    return taskvec;
}

namespace {
vector<Resource> parse_yaml_resources (const YAML::Node &resources)
{
    vector<Resource> resvec;

    /* "resources" must be a sequence */
    if (!resources.IsSequence()) {
        throw invalid_argument ("\"resources\" is not a sequence");
    }

    for (auto&& resource : resources) {
        resvec.push_back (Resource (resource));
    }

    return resvec;
}
}

Jobspec::Jobspec(const YAML::Node &top)
{
    /* The top yaml node of the jobspec must be a mapping */
    if (!top.IsMap()) {
        throw invalid_argument ("Top level of jobspec is not a mapping");
    }
    /* There must be exactly four entries in the mapping */
    if (top.size() != 4) {
        throw invalid_argument ("Top mapping in jobspec must have four entries");
    }
    /* The four keys must be the following */
    if (!top["version"]) {
        throw invalid_argument ("Missing key \"version\" in top level mapping");
    }
    if (!top["resources"]) {
        throw invalid_argument ("Missing key \"resource\" in top level mapping");
    }
    if (!top["tasks"]) {
        throw invalid_argument ("Missing key \"tasks\" in top level mapping");
    }
    if (!top["attributes"]) {
        throw invalid_argument ("Missing key \"attributes\" in top level mapping");
    }

    /* Import version */
    if (!top["version"].IsScalar()) {
        throw invalid_argument ("\"version\" must be an unsigned integer");
    }
    version = top["version"].as<unsigned int>();

    /* Import attributes mappings */
    YAML::Node attrs = top["attributes"];
    if (!attrs.IsMap()) {
        throw invalid_argument ("\"attributes\" is not a mapping");
    }
    for (auto&& i : attrs) {
        attributes[i.first.as<string>()];
        if (!i.second.IsMap()) {
            throw invalid_argument ("value of one of the attributes not a mapping");
        }
        for (auto&& j : i.second) {
            attributes[i.first.as<string>()][j.first.as<string>()] =
                j.second.as<string>();
        }
    }

    /* Import resources section */
    resources = parse_yaml_resources (top["resources"]);

    /* Import tasks section */
    tasks = parse_yaml_tasks (top["tasks"]);
}

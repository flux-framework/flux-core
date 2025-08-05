###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# Usage: python3 gen-cmdhelp.py path/to/sphinx/conf.py
#
# Generate flux-core cmdhelp from rst comments in manpages
# Reads sphinx conf.py to get list of section 1 manpages and
# their command names/descriptions
#

import json
import os
import os.path
import sys


class HelpEntries:
    def __init__(self):
        self.sections = [
            {
                "name": "submission",
                "description": "run and submit jobs, allocate resources",
            },
            {
                "name": "jobs",
                "description": "list and interact with jobs",
            },
            {
                "name": "instance",
                "description": "get resource, queue and other instance information",
            },
            {
                "name": "other",
                "description": "other useful commands",
            },
        ]
        for entry in self.sections:
            entry["commands"] = []

    def add_entry(self, name, description, section="other"):
        for entry in self.sections:
            if entry["name"] == section:
                info = dict(name=name, description=description)
                entry["commands"].append(info)


if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} sphinxconf.py", file=sys.stderr)

sphinxconf = sys.argv[1]
docsdir = os.path.abspath(os.path.dirname(sphinxconf))

# Don't exec the sphinxconf, instead import what we need
sys.path.insert(0, docsdir)

# Now that sys.path contains path to manpages, import them:
from manpages import man_pages  # noqa: E402

visited = dict()

entries = HelpEntries()
for path, cmd, descr, author, sec in man_pages:
    if sec != 1 or path in visited:
        continue
    visited[path] = True
    rst_file = os.path.join(docsdir, f"{path}.rst")
    with open(rst_file, "r", encoding="utf-8") as f:
        include_flag = False
        section = "other"
        for line in f:
            line = line.rstrip("\n")
            if ".. flux-help-" in line:
                include_flag = True
                if cmd.startswith("flux-"):
                    cmd = cmd[5:]
                if ".. flux-help-description: " in line:
                    descr = " ".join(line.split(" ")[2:])
                if ".. flux-help-command: " in line:
                    cmd = " ".join(line.split(" ")[2:])
                if ".. flux-help-section: " in line:
                    section = " ".join(line.split(" ")[2:])

        if include_flag:
            entries.add_entry(cmd, descr, section)

print(json.dumps(entries.sections, indent=2, sort_keys=True), file=sys.stdout)
sys.path.pop(0)

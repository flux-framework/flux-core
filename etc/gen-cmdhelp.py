#!/usr/bin/env python3

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

import sys
import json
from os import path

if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} sphinxconf.py", file=sys.stderr)

sphinxconf = sys.argv[1]
docsdir = path.dirname(sphinxconf)

exec(open(sphinxconf).read())

entries = []
visited = dict()

for (path, cmd, descr, author, section) in man_pages:
    if section != 1 or path in visited:
        continue
    visited[path] = True
    with open(f"{docsdir}/{path}.rst", "r", encoding='utf-8') as f:
        include_flag = False
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
        if include_flag:
            entry = dict(category="core", command=cmd, description=descr)
            entries.append(entry)

print(json.dumps(entries, indent=2, sort_keys=True), file=sys.stdout)

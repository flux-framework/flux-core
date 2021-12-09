###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import os
import re
from pathlib import PurePath

from flux.uri import URIResolverPlugin


def _get_broker_child(pid):
    """Return the pid of the first child found, for use with flux-broker"""
    for taskid in os.listdir(f"/proc/{pid}/task"):
        try:
            with open(f"/proc/{pid}/task/{taskid}/children") as cfile:
                cpid = cfile.read().split(" ")[0]
                if cpid:
                    return cpid
        except FileNotFoundError:
            pass
    raise ValueError(f"PID {pid} is a flux-broker and no child can be found")


class URIResolver(URIResolverPlugin):
    """A URIResolver that can fetch a FLUX_URI value from a local PID"""

    def describe(self):
        return "Get FLUX_URI for a given local PID"

    def resolve(self, uri):
        """
        Resolve a PID to a URI by grabbing the FLUX_URI environment variable
        from the process environment, or if the process is a flux-broker,
        then from the first child.
        """
        pid = PurePath(uri.path).parts[0]
        command = ""
        with open(f"/proc/{pid}/status") as sfile:
            command = sfile.read().split("\t")[1]
        if re.search("flux-broker", command):
            pid = _get_broker_child(pid)
        with open(f"/proc/{pid}/environ", encoding="utf-8") as envfile:
            for line in envfile.read().split("\0"):
                if "=" in line:
                    result = line.split("=")
                    if result[0] == "FLUX_URI":
                        return result[1]
        raise ValueError(f"PID {pid} doesn't seem to have a FLUX_URI")

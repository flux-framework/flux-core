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


def _get_broker_child_fallback(broker_pid):
    broker_pid = int(broker_pid)
    pids = [int(x) for x in os.listdir("/proc") if x.isdigit()]

    #  Sort list of pids based on distance from broker_pid
    #  (PID of broker child is likely close to broker_pid)
    pids.sort(key=lambda pid: abs(pid - broker_pid))

    #  Remove broker_pid since it definitely isn't a child
    pids.remove(broker_pid)

    #  Now iterate all processes, returning immediately when
    #   we've found a process for which broker_pid is the parent
    for pid in pids:
        try:
            with open(f"/proc/{pid}/stat") as statf:
                data = statf.read()
        except OSError:
            #  /proc/PID/stat is readable by all, but there is a chance
            #  of a TOU-TOC race here, so just ignore all errors
            pass
        else:
            match = re.match(r"^[0-9]+ \(.*\) \w+ ([0-9]+)", data)
            #  Attempt to convert match to integer. On regex match failure,
            #   or integer conversion failure, just skip this entry
            try:
                ppid = int(match.group(1))
                if ppid == broker_pid:
                    return pid
            except (IndexError, ValueError):
                pass
    raise ValueError(f"PID {broker_pid} is a flux-broker and no child found")


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
    raise ValueError(f"PID {pid} is a flux-broker and no child found")


def _proc_has_task_children():
    pid = os.getpid()
    for taskid in os.listdir(f"/proc/{pid}/task"):
        try:
            with open(f"/proc/{pid}/task/{taskid}/children") as cfile:
                return True
        except FileNotFoundError:
            return False
    return False


class URIResolver(URIResolverPlugin):
    """A URIResolver that can fetch a FLUX_URI value from a local PID"""

    #  Determine which broker_get_child method to use:
    if (
        _proc_has_task_children()
        and "FLUX_FORCE_BROKER_CHILD_FALLBACK" not in os.environ
    ):
        get_broker_child = staticmethod(_get_broker_child)
    else:
        get_broker_child = staticmethod(_get_broker_child_fallback)

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
            pid = self.get_broker_child(pid)
        with open(f"/proc/{pid}/environ", encoding="utf-8") as envfile:
            for line in envfile.read().split("\0"):
                if "=" in line:
                    result = line.split("=")
                    if result[0] == "FLUX_URI":
                        return result[1]
        raise ValueError(f"PID {pid} doesn't seem to have a FLUX_URI")

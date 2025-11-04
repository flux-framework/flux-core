###############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import os

try:
    from dataclasses import dataclass  # novermin
except ModuleNotFoundError:
    from flux.utils.dataclasses import dataclass

from flux.constants import FLUX_NODEID_ANY
from flux.rpc import RPC

# Subprocess waitable flag:
SUBPROCESS_REXEC_WAITABLE = 16


def command_create(command, label=None, cwd=None, env=None, opts=None):
    """
    Create an RFC 42 command object

    Args:
        command (list): command line to execute
        label (str): optional process label
        cwd (str): current working directory (default: current directory)
        env (dict): environment variables (default: copy of current environment)
        opts (dict): additional options (default: empty dict)

    Returns:
        dict: RFC 42 command object suitable for use with subprocess
              execution RPCs
    """
    if cwd is None:
        cwd = os.getcwd()
    if env is None:
        env = dict(os.environ)
    if opts is None:
        opts = {}
    command = {
        "cmdline": command,
        "label": label,
        "cwd": cwd,
        "env": env,
        "opts": opts,
        "channels": [],
    }
    if label is None:
        # Unset label: remove from payload
        del command["label"]
    return command


class SubprocessBackgroundRexecRPC(RPC):
    """
    RPC object returned from :func:`rexec_bg`
    """

    def __init__(self, handle, *args, **kwargs):
        # save nodeid from request so get_rank() works:
        nodeid = kwargs.get("nodeid", FLUX_NODEID_ANY)
        self.__nodeid = handle.get_rank() if nodeid == FLUX_NODEID_ANY else nodeid

        super().__init__(handle, *args, **kwargs)

    def get_pid(self):
        """Return process id of remote process"""
        return self.get()["pid"]

    def get_rank(self):
        """Return rank on which remote process was launched"""
        return self.__nodeid


def rexec_bg(
    handle,
    command,
    cwd=None,
    env=None,
    label=None,
    waitable=False,
    service="rexec",
    nodeid=FLUX_NODEID_ANY,
):
    """Run a process in the background

    Args:
        handle (:obj:`flux.Flux`): Flux handle
        command (list): command to run
        cwd (str): current working directory of remote process. Default: use
            the cwd of the current process.
        env (dict): environment of remote process. Default: use the current
            environment.
        label (str): Give the remote process an RFC 42 label.
        waitable (bool): Create the remote process with the waitable flag.
            Allows use of :func:`wait` function.
        service (str): Use an alternate service (default: ``rexec``)
        nodeid (int): Target a different node (default: FLUX_NODEID_ANY)

    Returns:
        :obj:`SubprocessBackgroundRexecRPC`: RPC object with get_pid()
            and get_rank() methods

    Raises:
        :obj:`OSError`: on failure to invoke remote process
    """
    payload = {
        "cmd": command_create(command, label=label, cwd=cwd, env=env),
        "flags": SUBPROCESS_REXEC_WAITABLE if waitable else 0,
    }
    return SubprocessBackgroundRexecRPC(
        handle, topic=f"{service}.exec", nodeid=nodeid, payload=payload
    )


def kill(
    handle, signum=15, pid=None, label=None, service="rexec", nodeid=FLUX_NODEID_ANY
):
    """Kill a subprocess

    Kill a subprocess running on rank ``nodeid`` using either ``pid`` or
    ``label``.

    Args:
        handle (:obj:`flux.Flux`): Flux handle
        signum (int): signal to send (default: SIGTERM)
        pid (int): remote subprocess id
        label (str): remote process label
        service (str): remote service to use (default: "rexec")
        nodeid (int): remote nodeid (default: FLUX_NODEID_ANY)

    Returns:
        :obj:`flux.rpc.RPC`

    Raises:
        :obj:`ValueError`: if neither or both of pid and label are specified
        :obj:`OSError`: on RPC failure
    """
    if pid is None and label is None:
        raise ValueError("at least one of pid or label must be specified")
    if pid is not None and label is not None:
        raise ValueError("only one of pid or label may be specified")
    if pid is not None:
        payload = {"pid": int(pid), "signum": int(signum)}
    else:
        payload = {"pid": -1, "label": label, "signum": int(signum)}

    return handle.rpc(topic=f"{service}.kill", nodeid=nodeid, payload=payload)


class SubprocessWaitRPC(RPC):
    """RPC object returned from :func:`wait`"""

    def get_status(self):
        """
        Return raw wait status from remote process

        Returns:
            int: wait status suitable for use with os.WIFEXITED(),
                os.WEXITSTATUS(), etc.
        """
        return self.get()["status"]


def wait(handle, pid=None, label=None, service="rexec", nodeid=FLUX_NODEID_ANY):
    """Wait on a remote waitable process

    Args:
        handle (:obj:`flux.Flux`): Flux handle
        pid (int): remote subprocess id
        label (str): remote process label
        service (str): remote service to use (default: "rexec")
        nodeid (int): remote nodeid (default: FLUX_NODEID_ANY)

    Returns:
        :obj:`SubprocessWaitRPC`

    Raises:
        :obj:`ValueError`: if neither or both of pid and label are specified
        :obj:`OSError`: on RPC failure
    """
    if pid is None and label is None:
        raise ValueError("at least one of pid or label must be specified")
    if pid is not None and label is not None:
        raise ValueError("only one of pid or label may be specified")
    if pid is not None:
        payload = {"pid": int(pid)}
    else:
        payload = {"pid": -1, "label": label}
    return SubprocessWaitRPC(
        handle, topic=f"{service}.wait", nodeid=nodeid, payload=payload
    )


@dataclass
class Subprocess:
    """
    Class representing a Flux subprocess object as returned from rexec.list
    """

    pid: int
    rank: int
    state: str
    label: str
    cmd: str

    def __post_init__(self):
        if not self.label:
            self.label = "-"


class SubprocessListRPC(RPC):

    def get_processes(self):
        """
        Get a list of Subprocess objects from an rexec.list response.

        Returns a list of :obj:`Subprocess` objects, one for each remote
        process.
        """
        resp = self.get()
        return [Subprocess(**x, rank=resp["rank"]) for x in resp["procs"]]


def list(handle, service="rexec", nodeid=FLUX_NODEID_ANY):
    """Get current subprocess list from a remote rank

    Args:
        handle (:obj:`flux.Flux`): Flux handle
        service (str): rexec service to use (default: "rexec")
        nodeid (int): nodeid to target (default: FLUX_NODEID_ANY)

    Returns:
        :obj:`SubprocessListRPC`
    """
    return SubprocessListRPC(handle, topic=f"{service}.list", nodeid=nodeid)


# vi: ts=4 sw=4 expandtab

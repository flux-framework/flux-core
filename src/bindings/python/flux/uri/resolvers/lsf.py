###############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""
This module supports the Flux URI resolver for IBM Spectrum LSF.

It can be used like:
  `flux uri lsf:<JOBID>`.

"""

import getpass
import os
import shutil
import subprocess
from pathlib import PurePath

import flux.hostlist as hostlist
import yaml
from flux.uri import FluxURIResolver, URIResolverPlugin


def lsf_find_compute_node(jobid):
    """Figure out where the job is being run using YAML output from IBM Cluster Systems Manager."""

    csm_path = os.getenv(
        "CSM_ALLOCATION_QUERY", "/opt/ibm/csm/bin/csm_allocation_query"
    )

    sp = subprocess.run(
        [csm_path, "-j", str(jobid)],
        stdout=subprocess.PIPE,
    )
    try:
        hosts = yaml.safe_load(sp.stdout.decode("utf-8"))["compute_nodes"]
        return str(hostlist.decode(hosts).sort()[0])
    except Exception as exc:
        raise ValueError(
            f"Unable to find a compute node attached to job {jobid}"
        ) from exc


def check_lsf_jobid(pid, jobid):
    try:
        with open(f"/proc/{pid}/environ", encoding="utf-8") as envfile:
            if f"LSB_JOBID={jobid}" in envfile.read():
                return True
    except FileNotFoundError:
        # if pid disappears while we try to read it, this is a False
        pass
    return False


def lsf_job_pid(jobid):
    """Resolve the pids of an LSF job using ssh to the compute node"""

    sp = subprocess.run(
        ["ps", "-Ho", "pid,comm", "-u", getpass.getuser()],
        stdout=subprocess.PIPE,
        check=True,
    )
    for line in sp.stdout.decode("utf-8").split("\n"):
        if "flux-broker" in line:
            pid = line.split()[0]
            if check_lsf_jobid(pid, jobid):
                return pid
    raise ValueError(f"Unable to find a flux session attached to your job")


def lsf_get_uri(hostname, jobid):
    """
    Resolve the URI of a Flux instance by running

      `ssh <HOSTNAME> flux uri lsf:<PID>?is_compute`

    and converting the result from bytecode to a string.
    """
    ssh_path = os.getenv("FLUX_SSH", "ssh")

    #  Allow path to remote flux to be overridden as in ssh connector,
    #   o/w, use path the current `flux` executable:
    flux_cmd_path = os.getenv("FLUX_SSH_RCMD", shutil.which("flux"))
    sp = subprocess.run(
        [
            ssh_path,
            hostname,
            flux_cmd_path,
            "uri",
            "--remote",
            f"lsf:{jobid}?is_compute",
        ],
        stdout=subprocess.PIPE,
        check=True,
    )
    return sp.stdout.decode("utf-8")


class URIResolver(URIResolverPlugin):
    """A URIResolver that can fetch a FLUX_URI from an LSF job id"""

    def describe(self):
        return "Get URI for a Flux instance launched under IBM Spectrum LSF"

    def resolve(self, uri):
        jobid = str(PurePath(uri.path).parts[0])

        if "is_compute" in uri.query_dict:
            uri = FluxURIResolver().resolve(f"pid:{lsf_job_pid(jobid)}")
            return uri.remote

        try:
            hostname = lsf_find_compute_node(jobid)
            return lsf_get_uri(hostname, jobid)
        except OSError as exc:
            raise ValueError(
                f"LSF Job {jobid} doesn't seem to have a FLUX_URI"
            ) from exc

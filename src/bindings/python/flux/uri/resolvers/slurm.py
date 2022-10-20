###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import errno
import os
import subprocess
from pathlib import PurePath

from flux.uri import FluxURIResolver, URIResolverPlugin


def slurm_job_pids(jobid):
    """Read pids for Slurm jobid using scontrol listpids"""

    pids = []
    sp = subprocess.run(
        ["scontrol", "listpids", jobid], stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    if sp.returncode != 0:
        raise OSError(errno.ENOENT)
    for line in sp.stdout.decode("utf-8").split("\n"):
        #  Output fields are "PID,JOBID,STEPID,LOCALID,GLOBALID"
        #
        #  The "main" flux-broker should have been directly launched
        #   from slurmstepd, so its LOCALID should be 0. Thus, only
        #   process pids with LOCALID 0 so we don't accidentally get
        #   the FLUX_URI for a subinstance of the main Flux instance
        #   (avoids the need to query the instance-level attribute)
        fields = line.split()
        try:
            if int(fields[3]) != 0:
                continue
            pid = int(fields[0])
            if pid != os.getpid():
                pids.append(pid)
        except (ValueError, IndexError):
            pass
    return pids


def slurm_resolve_remote(jobid):
    """
    Attempt to resolve a Flux job URI for Slurm jobid by running

      srun --overlap --jobid flux uri slurm:jobid

    on the first node of the Slurm job
    """

    #  Clear FLUX_URI in srun environment so we don't confuse
    #   ourselves and return the current FLUX_URI from flux-uri's
    #   environment
    env = os.environ.copy()
    if "FLUX_URI" in env:
        del env["FLUX_URI"]
    sp = subprocess.run(
        [
            "srun",
            "--overlap",
            f"--jobid={jobid}",
            "-n1",
            "-N1",
            "flux",
            "uri",
            f"slurm:{jobid}",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )
    if sp.returncode != 0:
        raise ValueError(f"Unable to resolve Flux URI for Slurm job {jobid}")
    return sp.stdout.decode("utf-8").rstrip()


class URIResolver(URIResolverPlugin):
    """A URIResolver that can fetch a FLUX_URI from a Slurm job"""

    def describe(self):
        return "Get URI for a Flux instance launched under Slurm"

    def resolve(self, uri):
        jobid = PurePath(uri.path).parts[0]

        #  Get list of local Slurm job pids from scontrol.
        #  If that fails, then the job might be running remotely, so
        #   try using srun to run `flux uri slurm:JOBID` on the first
        #   node of the Slurm job.
        try:
            pids = slurm_job_pids(jobid)
        except OSError:
            return slurm_resolve_remote(jobid)

        resolver = FluxURIResolver()
        for pid in pids:
            try:
                return resolver.resolve(f"pid:{pid}").remote
            except (ValueError, OSError):
                pass
        raise ValueError(f"PID {pid} doesn't seem to have a FLUX_URI")

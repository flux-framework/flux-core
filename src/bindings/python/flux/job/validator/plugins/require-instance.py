##############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

"""Require that all jobs are new instances of Flux

This plugin validates that submitted jobs are new instances of Flux.
That is, either the `batch` system attribute is set, or the first
2 arguments of the command are `flux broker` or `flux start`.

This is not a foolproof solution. The validator could reject jobs that
are new instances of Flux if an instance is launched by a script and
not directly via `flux broker` or `flux start`.
"""

import errno
from os.path import basename

from flux.job import JobspecV1
from flux.job.validator import ValidatorPlugin


class JobSize:

    def __init__(self, nnodes=0, ncores=0):
        self.nnodes = nnodes
        self.ncores = ncores

    @classmethod
    def from_jobspec(cls, jobspec):
        counts = JobspecV1(**jobspec).resource_counts()
        return JobSize(counts.get("node", None), counts.get("core", None))

    def ignore_by_size(self, jobspec):
        """
        Return True if jobspec size is less than the currently configured
        minimum job size required for a require-instance check
        """
        size = JobSize.from_jobspec(jobspec)
        if size.nnodes is not None and size.nnodes >= self.nnodes:
            return False
        if size.ncores is not None and size.ncores >= self.ncores:
            return False
        return True

    def configured(self):
        return self.nnodes > 0 or self.ncores > 0

    @property
    def errstr(self):
        result = "Direct job submission disabled"
        if self.nnodes == 0 and self.ncores == 0:
            result += " for all jobs in this instance"
        else:
            result += " for jobs >="
            if self.nnodes > 0:
                result += f" {self.nnodes} nodes"
            if self.ncores > 0:
                if self.nnodes > 0:
                    result += " or"
                result += f" {self.ncores} cores"
        result += ". Please use flux-batch(1) or flux-alloc(1)"
        return result


class Validator(ValidatorPlugin):
    def __init__(self, parser):
        parser.add_argument(
            "--require-instance-minnodes",
            help="minimum node count that requires flux-batch/alloc be used."
            + "default: 0",
            metavar="N",
            type=int,
            default=0,
        )
        parser.add_argument(
            "--require-instance-mincores",
            metavar="N",
            help="minimum core count that requires flux-batch/alloc be used."
            + "default: 0",
            type=int,
            default=0,
        )

    def configure(self, args):
        nnodes = args.require_instance_minnodes
        ncores = args.require_instance_mincores
        if nnodes > 0 and ncores == 0:
            # Default ncores to a multiple of nnodes to avoid leaving an
            # accidental hole that allows a cores-only job to be admitted
            # by this plugin:
            ncores = 16 * nnodes

        self.minsize = JobSize(nnodes, ncores)

    def validate(self, args):

        #  Ignore this job if it falls below a configured minsize:
        if self.minsize.ignore_by_size(args.jobspec):
            return

        #  Otherwise, ensure this job is a new instance of Flux:
        if "batch" in args.jobspec["attributes"]["system"]:
            return
        command = args.jobspec["tasks"][0]["command"]
        arg0 = basename(command[0])
        if arg0 == "flux" and command[1] in ["broker", "start"]:
            return
        return (errno.EINVAL, self.minsize.errstr)

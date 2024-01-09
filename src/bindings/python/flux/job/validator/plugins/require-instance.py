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

from flux.job.validator import ValidatorPlugin


class Validator(ValidatorPlugin):
    def validate(self, args):
        if "batch" in args.jobspec["attributes"]["system"]:
            return
        command = args.jobspec["tasks"][0]["command"]
        arg0 = basename(command[0])
        if arg0 == "flux" and command[1] in ["broker", "start"]:
            return
        return (
            errno.EINVAL,
            "Direct job submission is disabled for this instance."
            + " Please use flux-batch(1) or flux-alloc(1)",
        )

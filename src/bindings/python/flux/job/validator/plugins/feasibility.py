##############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

"""Use feasibility service to validate job

This plugin validates jobs via a feasibility.check RPC.

This allows jobs which are making infeasible or otherwise invalid
requests to be rejected by the scheduler before ingest, instead of
when the scheduler attempts to allocate resources for them.

ENOSYS errors from the RPC are ignored, in case there is no feasibility
service currently loaded.
"""

import errno

from flux.job.validator import ValidatorPlugin


class Validator(ValidatorPlugin):
    def __init__(self, parser):
        self.service_name = "feasibility.check"

    def validate(self, args):
        try:
            self.flux.rpc(self.service_name, args.jobinfo).get()
        except OSError as err:
            if err.errno == errno.ENOSYS:
                #  Treat ENOSYS as success
                return (0, None)
            return (err.errno, err.strerror)

##############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

"""Use sched.feasibility RPC to validate job

This plugin validates jobs via a sched.feasibility RPC.

This allows jobs which are making infeasible or otherwise invalid
requests to be rejected by the scheduler before ingest, instead of
when the scheduler attempts to allocate resources for them.

The RPC endpoint for feasibility checks can be set with an optional
--feasibility-service=NAME option.

ENOSYS errors from the RPC are ignored, in case the loaded scheduler
does not support the sched.feasibility RPC
"""

import errno

from flux.job.validator import ValidatorPlugin


class Validator(ValidatorPlugin):
    def __init__(self, parser):
        self.service_name = "sched.feasibility"
        parser.add_argument(
            "--feasibility-service",
            metavar="NAME",
            help="Set feasibility RPC service endpoint "
            f"(default={self.service_name})",
        )

    def configure(self, args):
        if args.feasibility_service:
            self.service_name = args.feasibility_service

    def validate(self, args):
        try:
            args.flux.rpc(self.service_name, args.jobinfo).get()
        except OSError as err:
            if err.errno == errno.ENOSYS:
                #  Treat ENOSYS as success
                return (0, None)
            return (err.errno, err.strerror)

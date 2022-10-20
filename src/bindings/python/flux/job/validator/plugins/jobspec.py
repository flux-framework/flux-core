##############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

"""Python bindings based jobspec validator

Uses the ``flux.job.validate_jobspec`` Python function to validate
submitted jobspec. If the optional ``--require-version`` option is
supplied, then only that version of jobspec is permitted.
"""

import json

from flux.job import validate_jobspec
from flux.job.validator import ValidatorPlugin


class Validator(ValidatorPlugin):
    def __init__(self, parser):
        self.require_version = 1
        parser.add_argument(
            "--require-version",
            metavar="V",
            default=1,
            help="Require jobspec version V (or any)",
        )

    def configure(self, args):
        try:
            self.require_version = int(args.require_version)
            if self.require_version < 1:
                raise ValueError(
                    f"Required jobspec version too low: {args.require_version} is < 1"
                )
            elif self.require_version > 1:
                raise ValueError(
                    f"Required jobspec version too high: {args.require_version} is > 1"
                )
        except ValueError:
            if args.require_version != "any":
                raise ValueError(f"Invalid argument to --require-version")
            self.require_version = None

    def validate(self, args):
        validate_jobspec(json.dumps(args.jobspec), self.require_version)

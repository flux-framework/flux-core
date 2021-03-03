##############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import sys
from flux.job.validator import ValidatorPlugin


class Validator(ValidatorPlugin):
    """Fake validator that exits on first validate() call"""

    def validate(self, args):
        print("Exiting with code 1 for testing", file=sys.stderr, flush=True)
        sys.exit(1)
